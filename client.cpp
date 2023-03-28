#include <arpa/inet.h>
#include <cassert>
#include <cstdint>
#include <errno.h>
#include <iostream>
#include <netinet/ip.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "constants.h"

static void die(const char *msg) {
  std::cerr << "Error " << errno << ": " << msg << "\n";
  abort();
}

static int32_t read_full(int fd, char *buf, size_t n) {
  while (n > 0) {
    ssize_t ret = recv(fd, buf, n, 0);
    if (ret <= 0) {
      return -1; // error, or unexpected EOF
    }
    assert((size_t)ret <= n);
    n -= (size_t)ret;
    buf += ret;
  }
  return 0;
}

static int32_t write_all(int fd, const char *buf, size_t n) {
  while (n > 0) {
    ssize_t ret = send(fd, buf, n, 0);
    if (ret <= 0) {
      return -1; // error
    }
    assert((size_t)ret <= n);
    n -= (size_t)ret;
    buf += ret;
  }
  return 0;
}

static int32_t send_request(int fd, const char *text) {
  uint32_t len{(uint32_t)strlen(text)};
  if (len > constants::MAX_MSG_SIZE) {
    return -1;
  }

  char wbuf[constants::MSG_BUFFER_SIZE];
  memcpy(wbuf, &len, constants::MSG_LEN_BYTES); // assume little endian
  memcpy(&wbuf[constants::MSG_LEN_BYTES], text, len);
  return write_all(fd, wbuf, constants::MSG_LEN_BYTES + len);
}

static int32_t read_response(int fd) {
  char rbuf[constants::MSG_BUFFER_SIZE + 1]; // Extra byte for null terminator
  errno = 0;
  int32_t err = read_full(fd, rbuf, constants::MSG_LEN_BYTES);
  if (err) {
    if (errno == 0) {
      std::cerr << "EOF\n";
    } else {
      std::cerr << "read() error\n";
    }
    return err;
  }

  uint32_t len{};
  memcpy(&len, rbuf, constants::MSG_LEN_BYTES); // assume little endian
  if (len > constants::MAX_MSG_SIZE) {
    std::cerr << "too long\n";
    return -1;
  }

  err = read_full(fd, &rbuf[constants::MSG_LEN_BYTES], len);
  if (err) {
    std::cerr << "read() error\n";
    return err;
  }

  rbuf[constants::MSG_LEN_BYTES + len] = '\0';
  printf("server says: %s\n", &rbuf[constants::MSG_LEN_BYTES]);
  return 0;
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    die("socket()");
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);
  addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK); // 127.0.0.1
  int rv{connect(fd, (const sockaddr *)&addr, sizeof(addr))};
  if (rv) {
    die("connect() error");
  }

  // multiple pipelined requests
  const char *query_list[3] = {"hello1", "hello2", "hello3"};
  for (size_t i = 0; i < 3; ++i) {
    if (send_request(fd, query_list[i])) {
      goto L_DONE;
    }
  }
  for (size_t i = 0; i < 3; ++i) {
    if (read_response(fd)) {
      goto L_DONE;
    }
  }

L_DONE:
  close(fd);
  return 0;
}