#include <arpa/inet.h>
#include <asm-generic/errno-base.h>
#include <cassert>
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

enum class CONN_STATE { REQ, RES, END };

struct Conn {
  int fd{-1};
  CONN_STATE state{CONN_STATE::END};

  // Read buffer
  // Stores the data of client requests
  uint8_t rbuf[constants::BUFFER_SIZE];
  size_t rbuf_size{};

  // Write buffer
  // Stores the data for responses
  uint8_t wbuf[constants::BUFFER_SIZE];
  size_t wbuf_size{};
  size_t wbuf_sent{};
};

static void die(const std::string &msg);
static void check_errno(Conn &conn, const std::string &op);

static void accept_conn(int listenfd, int epollfd,
                        std::vector<std::unique_ptr<Conn>> &conn_map);
static void set_fd_nonblocking(int fd);

static void read_request(Conn &conn);
static bool handle_request(Conn &conn);
static bool read_into_buffer(Conn &conn);

static void write_response(Conn &conn);
static bool write_from_buffer(Conn &conn);

static void die(const std::string &msg) {
  std::cerr << "Error " << errno << ": " << msg << "\n";
  abort();
}

static void check_errno(Conn &conn, const std::string &op) {
  // EAGAIN means that the requested operation would block. Other errors mean
  // something wrong happened with the operation
  if (errno != EAGAIN) {
    std::cerr << op << " error\n";
    conn.state = CONN_STATE::END;
  }
}

// Makes it so that send() and recv() are nonblocking
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

static void read_request(Conn &conn) {
  while (read_into_buffer(conn)) {
  };
}

// Returns true if there is still more data to receive into the buffer, false
// otherwise
static bool read_into_buffer(Conn &conn) {
  ssize_t ret{0};

  do {
    size_t rem{sizeof(conn.rbuf) - conn.rbuf_size};
    ret = recv(conn.fd, &conn.rbuf[conn.rbuf_size], rem, 0);
  } while (ret < 0 && errno == EINTR); // Retry if interrupted

  if (ret < 0) {
    check_errno(conn, "recv()");
    return false;
  }

  if (ret == 0) {
    if (conn.rbuf_size > 0) {
      std::cerr << "unexpected EOF\n";
    } else {
      std::cerr << "eof\n";
    }
    conn.state = CONN_STATE::END;
    return false;
  }

  conn.rbuf_size += (size_t)ret;
  while (handle_request(conn)) {
  }
  return conn.state == CONN_STATE::REQ;
}

static bool handle_request(Conn &conn) {
  if (conn.rbuf_size < constants::MSG_LEN_BYTES) {
    return false;
  }

  uint32_t len;
  memcpy(&len, &conn.rbuf[0], constants::MSG_LEN_BYTES);
  if (len > constants::MAX_MSG_SIZE) {
    std::cerr << "Message too big\n";
    conn.state = CONN_STATE::END;
    return false;
  }
  if (constants::MSG_LEN_BYTES + len > conn.rbuf_size) {
    // Not enough room in the buffer
    return false;
  }

  printf("Client message: %.*s\n", len, &conn.rbuf[constants::MSG_LEN_BYTES]);

  memcpy(&conn.wbuf[0], &len, constants::MSG_LEN_BYTES);
  memcpy(&conn.wbuf[constants::MSG_LEN_BYTES],
         &conn.rbuf[constants::MSG_LEN_BYTES], len);
  conn.wbuf_size = constants::MSG_LEN_BYTES + len;

  size_t rem{conn.rbuf_size - constants::MSG_LEN_BYTES - len};
  if (rem) {
    memmove(conn.rbuf, &conn.rbuf[constants::MSG_LEN_BYTES + len], rem);
  }
  conn.rbuf_size = rem;

  conn.state = CONN_STATE::RES;
  write_response(conn);

  return conn.state == CONN_STATE::REQ;
}

static void write_response(Conn &conn) {
  while (write_from_buffer(conn)) {
  };
}

// Returns true if there is still more data to be sent from the buffer, false
// otherwise
static bool write_from_buffer(Conn &conn) {
  ssize_t ret{};

  do {
    size_t rem = conn.wbuf_size - conn.wbuf_sent;
    ret = send(conn.fd, &conn.wbuf[conn.wbuf_sent], rem, 0);
  } while (ret < 0 && errno == EINTR);

  if (ret < 0) {
    check_errno(conn, "send()");
    return false;
  }

  conn.wbuf_sent += ret;

  if (conn.wbuf_sent == conn.wbuf_size) {
    // Finished sending response
    conn.state = CONN_STATE::REQ;
    conn.wbuf_sent = 0;
    conn.wbuf_size = 0;
    return false;
  }

  // Still have some data to send, try again
  return true;
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
  conn->state = CONN_STATE::REQ;

  epoll_event ev{};
  ev.events = conn->state == CONN_STATE::REQ ? EPOLLIN : EPOLLOUT;
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

  std::vector<std::unique_ptr<Conn>> conn_map;
  epoll_event *events = new epoll_event[16 * 1024];
  if (events == nullptr) {
    die("Unable to allocate memory for epoll_events");
  }
  
  while (true) {
    int n{epoll_wait(epollfd, events, 16 * 1024, -1)};
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
      if (!conn) {
        assert(0);
      }

      if (conn->state == CONN_STATE::REQ) {
        read_request(*conn);
      } else if (conn->state == CONN_STATE::RES) {
        write_response(*conn);
      } else {
        die("not supposed to happen");
      }

      if (conn->state == CONN_STATE::END) {
        // Close the connection
        if (epoll_ctl(epollfd, EPOLL_CTL_DEL, conn->fd, nullptr) < 0) {
          die("epoll_ctl() EPOLL_CTL_DEL");
        }
        close(conn->fd);
        conn.reset();
      } else {
        // Update epoll event
        epoll_event ev{};
        ev.events = conn->state == CONN_STATE::REQ ? EPOLLIN : EPOLLOUT;
        ev.data.fd = conn->fd;
        if (epoll_ctl(epollfd, EPOLL_CTL_MOD, conn->fd, &ev) < 0) {
          die("epoll_ctl() EPOLL_CTL_MOD");
        }
      }
    }
  }

  return 0;
}