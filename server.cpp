#include <arpa/inet.h>
#include <asm-generic/errno-base.h>
#include <cassert>
#include <cstdint>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <netinet/ip.h>
#include <ostream>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "constants.h"

struct Status {
  bool req{false};
  bool res{false};
};

struct Conn {
  int fd{-1};

  // Stores info of what events to poll for
  Status state{};

  // Read buffer
  // Stores the data of client requests
  uint8_t rbuf[constants::READ_BUFFER_SIZE];
  size_t rbuf_size{};
  size_t rbuf_read{};

  // Write buffer
  // Stores the data for responses
  uint8_t wbuf[constants::WRITE_BUFFER_SIZE];
  size_t wbuf_size{};
  size_t wbuf_sent{};
};

static void die(const std::string &msg);
static void check_err(Conn &conn, const std::string &op);

static void accept_conn(int listenfd, int epollfd,
                        std::vector<std::unique_ptr<Conn>> &conn_map);
static void set_fd_nonblocking(int fd);

static void read_into_buffer(Conn &conn);
static void parse_request(Conn &conn);
static int create_response(Conn &conn, uint32_t len);

static void write_from_buffer(Conn &conn);

static void die(const std::string &msg) {
  std::cerr << "Error " << errno << ": " << msg << "\n";
  abort();
}

static void check_err(Conn &conn, const std::string &op) {
  // EAGAIN means that the requested operation would have blocked. Other errors
  // mean something wrong happened with the operation
  if (errno != EAGAIN) {
    std::cerr << op << " error\n";
    conn.state = {.req = false, .res = false};
  }
}

// Set the connection to be nonblocking
static void set_fd_nonblocking(int fd) {
  errno = 0;
  int flags{fcntl(fd, F_GETFL, 0)};
  if (errno) {
    die("fcntl error");
    return;
  }

  flags |= O_NONBLOCK;

  errno = 0;
  fcntl(fd, F_SETFL, flags);
  if (errno) {
    die("fcntl error");
  }
}

// Returns true if there is still more data to receive into the buffer, false
// otherwise
static void read_into_buffer(Conn &conn) {
  ssize_t ret{0};

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
  do {
    size_t rem{sizeof(conn.rbuf) - conn.rbuf_size};
    ret = recv(conn.fd, &conn.rbuf[conn.rbuf_size], rem, 0);
  } while (ret < 0 && errno == EINTR); // Retry if interrupted

  if (ret < 0) {
    check_err(conn, "recv()");
    return;
  }

  if (ret == 0) {
    if (conn.rbuf_size > 0) {
      std::cerr << "Unexpected EOF. Closing client connection\n";
    } else {
      std::cerr << "EOF. Closing client connection\n";
    }
    conn.state = {.req = false, .res = false};
    return;
  }

  conn.rbuf_size += (size_t)ret;
  parse_request(conn);

  return;
}

static void parse_request(Conn &conn) {
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
      // Buffer hasn't received the whole request yet, try again later
      break;
    }

    if (create_response(conn, len)) {
      break;
    }
  }
}

// Writes the response into the connection's write buffer. Returns 0 if
// successful, -1 if there is insufficient space in the buffer.
static int create_response(Conn &conn, uint32_t len) {
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
static void write_from_buffer(Conn &conn) {
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

static void accept_conn(int listenfd, int epollfd,
                        std::vector<std::unique_ptr<Conn>> &conn_map) {
  sockaddr_in client_addr{};
  socklen_t len{sizeof(client_addr)};
  int connfd{accept(listenfd, (sockaddr *)&client_addr, &len)};
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
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn->fd, &ev) < 0) {
    die("epoll_ctl() failed to add new connection");
  }

  if (conn_map.size() <= (size_t)conn->fd) {
    conn_map.resize(conn->fd + 1);
  }
  assert(!conn_map[conn->fd]);
  conn_map[conn->fd] = std::move(conn);
}

int main() {
  int listenfd{socket(AF_INET, SOCK_STREAM, 0)};
  if (listenfd < 0) {
    die("socket() error");
  }

  // this is needed for most server applications
  int opt_val{1};
  setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val));

  // bind
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);
  addr.sin_addr.s_addr = ntohl(0); // wildcard address 0.0.0.0
  if (bind(listenfd, (const sockaddr *)&addr, sizeof(addr))) {
    die("bind() error");
  }

  // listen
  if (listen(listenfd, SOMAXCONN)) {
    die("listen() error");
  }

  set_fd_nonblocking(listenfd);

  int epollfd{epoll_create1(0)};
  if (epollfd < 0) {
    die("epoll_create1() error");
  }

  epoll_event accept{};
  accept.events = EPOLLIN;
  accept.data.fd = listenfd;
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &accept) < 0) {
    die("epoll_ctl() EPOLL_CTL_ADD");
  }

  // Maps fd value to the respective Conn object
  std::vector<std::unique_ptr<Conn>> conn_map;

  epoll_event *events =
      new epoll_event[constants::EVENT_BUFFER_SIZE]; // TODO: change to
                                                     // unique_ptr
  if (events == nullptr) {
    die("Unable to allocate memory for epoll_events");
  }

  while (true) {
    int n{epoll_wait(epollfd, events, constants::EVENT_BUFFER_SIZE, -1)};
    if (n == 0) {
      die("poll() timed out");
    } else if (n < 0) {
      die("poll() error");
    }

    for (int i = 0; i < n; i++) {
      if (events[i].data.fd == listenfd) {
        // Check listening fd for new connections
        accept_conn(listenfd, epollfd, conn_map);
        continue;
      }

      auto &conn = conn_map[events[i].data.fd];
      assert(conn);

      // Always prioritise sending from the write buffer first in case it is
      // full.
      if (events[i].events & EPOLLOUT) {
        write_from_buffer(*conn);
      } else if (events[i].events & EPOLLIN) {
        read_into_buffer(*conn);
      } else {
        die("not supposed to happen");
      }

      if (!conn->state.req && !conn->state.res) {
        // Close the connection
        if (epoll_ctl(epollfd, EPOLL_CTL_DEL, conn->fd, nullptr) < 0) {
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
        if (epoll_ctl(epollfd, EPOLL_CTL_MOD, conn->fd, &ev) < 0) {
          die("epoll_ctl() EPOLL_CTL_MOD");
        }
      }
    }
  }

  return 0;
}