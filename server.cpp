#include <arpa/inet.h>
#include <cassert>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <netinet/ip.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

const size_t MAX_MESSAGE_SIZE{4096};
const int TIMEOUT{1000};

enum class CONN_STATE { REQ, RES, END };

struct Conn {
  int fd{-1};
  CONN_STATE state{CONN_STATE::END}; // either STATE_REQ or STATE_RES

  // Read buffer
  // Stores the data of client requests
  uint8_t rbuf[4 + MAX_MESSAGE_SIZE];
  size_t rbuf_size{};

  // Write buffer
  // Stores the data for responses
  uint8_t wbuf[4 + MAX_MESSAGE_SIZE];
  size_t wbuf_size{};
  size_t wbuf_sent{};
};

static void die(const char *msg) {
  fprintf(stderr, "Error number:%d\n %s\n", errno, msg);
  abort();
}

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

static bool read_into_buffer(Conn &conn) {
  // TODO
  return true;
}

static void handle_request(Conn &conn) {
  while (read_into_buffer(conn)) {
  };
}

static bool write_from_buffer(Conn &conn) {
  // TODO
  return true;
}

static void handle_response(Conn &conn) {
  while (write_from_buffer(conn))
    ;
}

static void accept_conn(int fd, std::vector<std::unique_ptr<Conn>> &conn_map) {
  sockaddr_in client_addr{};
  socklen_t len{sizeof(client_addr)};
  int connfd{accept(fd, (sockaddr *)&client_addr, &len)};
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

  if (conn_map.size() <= (size_t)conn->fd) {
    conn_map.resize(conn->fd + 1);
    assert(!conn_map[conn->fd]);
    conn_map[conn->fd] = std::move(conn);
  }
}

int main() {
  int fd{socket(AF_INET, SOCK_STREAM, 0)};
  if (fd < 0) {
    die("socket() error");
  }

  // this is needed for most server applications
  int opt_val{1};
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val));

  // bind
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);
  addr.sin_addr.s_addr = ntohl(0); // wildcard address 0.0.0.0
  if (bind(fd, (const sockaddr *)&addr, sizeof(addr))) {
    die("bind() error");
  }

  // listen
  if (listen(fd, SOMAXCONN)) {
    die("listen() error");
  }

  set_fd_nonblocking(fd);

  std::vector<std::unique_ptr<Conn>> conn_map;
  std::vector<pollfd> poll_args;
  while (true) {
    poll_args.clear();
    // Add the listening fd first
    poll_args.push_back({fd, POLLIN, 0});

    for (auto &conn : conn_map) {
      if (!conn) {
        continue;
      }

      // Store the polled information of connections
      pollfd pfd{conn->fd, POLLERR, 0};
      pfd.events |= (conn->state == CONN_STATE::REQ) ? POLLIN : POLLOUT;
      poll_args.push_back(pfd);
    }

    int ev{poll(poll_args.data(), (nfds_t)poll_args.size(), TIMEOUT)};
    if (ev == 0) {
      die("Poll() timed out");
    } else if (ev < 0) {
      die("Poll() error");
    }

    // Process active connections
    for (size_t i = 1; i < poll_args.size(); i++) {
      // Check if something happened in the connection
      if (poll_args[i].revents) {
        auto &conn = conn_map[poll_args[i].fd];

        switch (conn->state) {
        case CONN_STATE::REQ:
          // Read the request from the connection
          handle_request(*conn);
          break;
        case CONN_STATE::RES:
          // Write the response back to the connection
          handle_response(*conn);
          break;
        case CONN_STATE::END:
          // Close the connection
          close(conn->fd);
          conn.reset();
          break;
        }
      }
    }

    // Check listening fd for new connections
    if (poll_args[0].revents) {
      accept_conn(fd, conn_map);
    }
  }

  return 0;
}