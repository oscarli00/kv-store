#include "client.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <netinet/ip.h>
#include <ostream>
#include <sstream>
#include <unistd.h>
#include <vector>

#include "constants.h"
#include "utils.h"

Client::Client() { init(); }

Client::~Client() { close(fd_); }

void Client::init() {
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (fd_ < 0) {
    die("socket()");
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);
  addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK); // 127.0.0.1
  int rv{};
  do {
    rv = connect(fd_, (const sockaddr *)&addr, sizeof(addr));
    if (rv) {
      std::cout << "Failed to connect to server." << std::endl;
      for (int i = 5; i > 0; i--) {
        std::cout << "Retrying in " << i << " second" << (i > 1 ? "s" : "")
                  << std::flush;
        sleep(1);
        std::cout << "\33[2K\r";
      }
      std::cout << "\n";
    }
  } while (rv);
  std::cout << "Connected to server.\n";
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

int32_t Client::send_request(const std::vector<std::string> &args) {
  uint32_t len{constants::MSG_NSTR_BYTES};
  for (const auto &s : args) {
    len += constants::ARG_LEN_BYTES + s.size();
  }
  if (len > constants::MAX_MSG_SIZE) {
    return -1;
  }


  char wbuf[constants::MSG_LEN_BYTES + constants::MAX_MSG_SIZE];
  memcpy(wbuf, &len, constants::MSG_LEN_BYTES);
  uint32_t nstr{static_cast<uint32_t>(args.size())};
  memcpy(&wbuf[constants::MSG_LEN_BYTES], &nstr, constants::MSG_NSTR_BYTES);

  uint32_t pos{constants::MSG_LEN_BYTES + constants::MSG_NSTR_BYTES};
  for (const auto &s : args) {
    uint32_t sz{static_cast<uint32_t>(s.size())};
    memcpy(&wbuf[pos], &sz, constants::ARG_LEN_BYTES);
    memcpy(&wbuf[pos + constants::ARG_LEN_BYTES], s.data(), sz);
    pos += constants::ARG_LEN_BYTES + sz;
  }

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
  printf("Server says>> %s\n", &rbuf[constants::MSG_LEN_BYTES]);
  return 0;
}

void Client::loop() {
  std::cout << "Enter a command: ";
  std::string in, s;
  getline(std::cin, in);
  std::istringstream stream(in);
  std::vector<std::string> args;
  while (stream >> s) {
    args.push_back(s);
  }
  send_request(args);
  read_response();
}

int main() {
  Client client;

  while (true) {
    client.loop();
  }

  return 0;
}