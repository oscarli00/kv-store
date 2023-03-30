#include "server.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/ip.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>

#include "constants.h"
#include "utils.h"

Server::Server() { init(); }

Server::~Server() {
  close(listen_fd_);
  close(epoll_fd_);
  delete[] events_;
}

void Server::init() {
  create_socket();

  bind_socket();

  start_listen();

  set_fd_nonblocking(listen_fd_);

  setup_epoll();
}

void Server::create_socket() {
  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    die("socket() error");
  }

  int opt_val{1};
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val));
}

void Server::bind_socket() {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);
  addr.sin_addr.s_addr = ntohl(0); // wildcard address 0.0.0.0
  if (bind(listen_fd_, (const sockaddr *)&addr, sizeof(addr))) {
    die("bind() error");
  }
}

void Server::start_listen() {
  if (listen(listen_fd_, SOMAXCONN)) {
    die("listen() error");
  }
}

void Server::setup_epoll() {
  epoll_fd_ = epoll_create1(0);
  if (epoll_fd_ < 0) {
    die("epoll_create1() error");
  }

  // Add listener to polled events
  epoll_event accept{};
  accept.events = EPOLLIN;
  accept.data.fd = listen_fd_;
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &accept) < 0) {
    die("epoll_ctl() EPOLL_CTL_ADD");
  }

  events_ = new epoll_event[constants::EVENT_BUFFER_SIZE];
  if (events_ == nullptr) {
    die("Unable to allocate memory for epoll_events");
  }
}

void Server::loop() {
  int n{epoll_wait(epoll_fd_, events_, constants::EVENT_BUFFER_SIZE, -1)};
  if (n == 0) {
    die("poll() timed out");
  } else if (n < 0) {
    die("poll() error");
  }

  for (int i = 0; i < n; i++) {
    if (events_[i].data.fd == listen_fd_) {
      // Check listening fd for new connections
      accept_conn();
      continue;
    }

    auto &conn = conn_map_[events_[i].data.fd];
    assert(conn);

    // Always prioritise sending from the write buffer first in case it is
    // full.
    if (events_[i].events & EPOLLOUT) {
      write_from_buffer(*conn);
    } else if (events_[i].events & EPOLLIN) {
      read_into_buffer(*conn);
    } else {
      die("not supposed to happen");
    }

    if (!conn->state.req && !conn->state.res) {
      // Close the connection
      if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, conn->fd, nullptr) < 0) {
        die("epoll_ctl() EPOLL_CTL_DEL");
      }
      close(conn->fd);
      conn.reset();
    } else {
      // Update epoll event
      epoll_event ev{};
      if (conn->state.req) {
        ev.events |= EPOLLIN;
      }
      if (conn->state.res) {
        ev.events |= EPOLLOUT;
      }
      ev.data.fd = conn->fd;
      if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, conn->fd, &ev) < 0) {
        die("epoll_ctl() EPOLL_CTL_MOD");
      }
    }
  }
}

void Server::accept_conn() {
  sockaddr_in client_addr{};
  socklen_t len{sizeof(client_addr)};
  int connfd{accept(listen_fd_, (sockaddr *)&client_addr, &len)};
  if (connfd < 0) {
    std::cerr << "accept() error\n";
    return;
  }

  set_fd_nonblocking(connfd);

  auto conn{std::make_unique<Conn>()};
  if (!conn) {
    close(connfd);
    return;
  }
  conn->fd = connfd;
  conn->state = {.req = true, .res = false};

  epoll_event ev{};
  if (conn->state.req) {
    ev.events |= EPOLLIN;
  }
  if (conn->state.res) {
    ev.events |= EPOLLOUT;
  }
  ev.data.fd = conn->fd;
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, conn->fd, &ev) < 0) {
    die("epoll_ctl() failed to add new connection");
  }

  if (conn_map_.size() <= (size_t)conn->fd) {
    conn_map_.resize(conn->fd + 1);
  }
  assert(!conn_map_[conn->fd]);
  conn_map_[conn->fd] = std::move(conn);
}

// Returns true if there is still more data to receive into the buffer, false
// otherwise
void Server::read_into_buffer(Conn &conn) {
  if (conn.rbuf_read > 0) {
    // Try to clear read buffer of requests that have already been processed
    size_t rem{conn.rbuf_size - conn.rbuf_read};
    if (rem) {
      memmove(conn.rbuf, &conn.rbuf[conn.rbuf_read], rem);
    }
    conn.rbuf_size = rem;
    conn.rbuf_read = 0;
  }

  // Read as much data as we can without blocking
  ssize_t ret{};
  do {
    size_t rem{sizeof(conn.rbuf) - conn.rbuf_size};
    ret = recv(conn.fd, &conn.rbuf[conn.rbuf_size], rem, 0);
  } while (ret < 0 && errno == EINTR); // Retry if interrupted

  if (ret < 0) {
    check_err(conn, "recv()");
  } else if (ret == 0) {
    if (conn.rbuf_size > 0) {
      std::cerr << "Unexpected EOF. Closing client connection\n";
    } else {
      std::cerr << "EOF. Closing client connection\n";
    }
    conn.state = {.req = false, .res = false};
  } else {
    conn.rbuf_size += (size_t)ret;
    parse_request(conn);
  }
}

void Server::parse_request(Conn &conn) {
  assert(conn.rbuf_read == 0);

  while (conn.rbuf_size - conn.rbuf_read >= constants::MSG_LEN_BYTES) {
    uint32_t len;
    memcpy(&len, &conn.rbuf[conn.rbuf_read], constants::MSG_LEN_BYTES);

    if (len > constants::MAX_MSG_SIZE) {
      std::cerr << "Message too big. Closing client connection\n";
      conn.state = {.req = false, .res = false};
      break;
    }

    if (constants::MSG_LEN_BYTES + len > conn.rbuf_size - conn.rbuf_read) {
      // Read buffer hasn't received the whole request yet, try again later
      break;
    }

    if (create_response(conn, len)) {
      break;
    }
  }
}

// Writes the response into the connection's write buffer. Returns 0 if
// successful, -1 if there is insufficient space in the buffer.
int Server::create_response(Conn &conn, uint32_t len) {
  assert(sizeof(len) == constants::MSG_LEN_BYTES);

  printf("Client message: %.*s\n", len,
         &conn.rbuf[conn.rbuf_read + constants::MSG_LEN_BYTES]);

  if (conn.wbuf_size + constants::MSG_LEN_BYTES + len >
      constants::WRITE_BUFFER_SIZE) {
    std::cerr << "no more space in write buffer\n";
    // Do not allow any more requests to be received.
    conn.state = {.req = false, .res = true};
    return -1;
  }

  // Copy response length into write buffer
  memcpy(&conn.wbuf[conn.wbuf_size], &len, constants::MSG_LEN_BYTES);
  // Copy response message into write buffer
  memcpy(&conn.wbuf[conn.wbuf_size + constants::MSG_LEN_BYTES],
         &conn.rbuf[conn.rbuf_read + constants::MSG_LEN_BYTES], len);

  conn.wbuf_size += constants::MSG_LEN_BYTES + len;
  conn.rbuf_read += constants::MSG_LEN_BYTES + len;
  conn.state.res = true;

  return 0;
}

// Send as much as we can from the write buffer
void Server::write_from_buffer(Conn &conn) {
  ssize_t ret{};
  do {
    size_t rem = conn.wbuf_size - conn.wbuf_sent;
    ret = send(conn.fd, &conn.wbuf[conn.wbuf_sent], rem, 0);
  } while (ret < 0 && errno == EINTR);

  if (ret < 0) {
    check_err(conn, "send()");
    return;
  }

  conn.wbuf_sent += ret;

  if (conn.wbuf_sent == conn.wbuf_size) {
    // Finished sending all responses
    conn.state.res = false;
    conn.wbuf_sent = 0;
    conn.wbuf_size = 0;
  }

  // Enable receiving requests again. (Would have only been disabled if the
  // write buffer ran out of space)
  conn.state.req = true;
}

void Server::check_err(Conn &conn, const std::string &op) {
  // EAGAIN means that the requested operation would have blocked. Other errors
  // mean something wrong happened with the operation
  if (errno != EAGAIN) {
    std::cerr << op << " error\n";
    conn.state = {.req = false, .res = false};
  }
}

int main() {
  Server sv;
  while (true) {
    sv.loop();
  }

  return 0;
}