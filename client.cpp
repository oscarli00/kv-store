#include "client.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <netinet/ip.h>
#include <unistd.h>

#include "constants.h"
#include "utils.h"

Client::Client() {
  init();
}

Client::~Client() {
  close(fd_);
}

void Client::init() {
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (fd_ < 0) {
    die("socket()");
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);
  addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK); // 127.0.0.1
  int rv{connect(fd_, (const sockaddr *)&addr, sizeof(addr))};
  if (rv) {
    die("connect() error");
  }
}

int32_t Client::read_full(char *buf, size_t n) {
  while (n > 0) {
    ssize_t ret = recv(fd_, buf, n, 0);
    if (ret <= 0) {
      return -1; // error, or unexpected EOF
    }
    assert((size_t)ret <= n);
    n -= (size_t)ret;
    buf += ret;
  }
  return 0;
}

int32_t Client::write_all(const char *buf, size_t n) {
  while (n > 0) {
    ssize_t ret = send(fd_, buf, n, 0);
    if (ret <= 0) {
      return -1; // error
    }
    assert((size_t)ret <= n);
    n -= (size_t)ret;
    buf += ret;
  }
  return 0;
}

int32_t Client::send_request(const char *text) {
  uint32_t len{(uint32_t)strlen(text)};
  if (len > constants::MAX_MSG_SIZE) {
    return -1;
  }

  char wbuf[constants::MSG_LEN_BYTES + constants::MAX_MSG_SIZE];
  memcpy(wbuf, &len, constants::MSG_LEN_BYTES); // assume little endian
  memcpy(&wbuf[constants::MSG_LEN_BYTES], text, len);
  return write_all(wbuf, constants::MSG_LEN_BYTES + len);
}

int32_t Client::read_response() {
  char rbuf[constants::MSG_LEN_BYTES + constants::MAX_MSG_SIZE +
            1]; // Extra byte for null terminator
  errno = 0;
  int32_t err{read_full(rbuf, constants::MSG_LEN_BYTES)};
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

  err = read_full(&rbuf[constants::MSG_LEN_BYTES], len);
  if (err) {
    std::cerr << "read() error\n";
    return err;
  }

  rbuf[constants::MSG_LEN_BYTES + len] = '\0';
  printf("server says: %s\n", &rbuf[constants::MSG_LEN_BYTES]);
  return 0;
}

int main() {
  Client client;

  const char *query_list[3] = {"hello1", "hello2", "hello3"};
  for (size_t i = 0; i < 3; ++i) {
    if (client.send_request(query_list[i])) {
      return 0;
    }
  }
  for (size_t i = 0; i < 3; ++i) {
    if (client.read_response()) {
      return 0;
    }
  }

  return 0;
}