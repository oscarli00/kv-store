#ifndef CLIENT_H
#define CLIENT_H

#include <cstdlib>

class Client {
public:
  Client();
  ~Client();

  int32_t send_request(const char *text);
  int32_t read_response();

private:
  int fd_{};

  void init();
  int32_t read_full(char *buf, size_t n);
  int32_t write_all(const char *buf, size_t n);
};

#endif