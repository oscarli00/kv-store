
#include "utils.h"

#include "server.h"
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <string>

void die(const std::string &msg) {
  std::cerr << "Error " << errno << ": " << msg << "\n";
  exit(EXIT_FAILURE);
}

// Set the connection to be nonblocking
void set_fd_nonblocking(int fd) {
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

