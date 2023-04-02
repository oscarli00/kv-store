
#include "utils.h"

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
  }

  flags |= O_NONBLOCK;

  errno = 0;
  fcntl(fd, F_SETFL, flags);
  if (errno) {
    die("fcntl error");
  }
}

// Case-insensitive string comparison. Doesn't work with special characters
bool strn_equals(std::string s1, std::string s2) {
  if (s1.size() != s2.size()) {
    return false;
  }
  for (auto c1 = s1.begin(), c2 = s2.begin(); c1 != s1.end(); ++c1, ++c2) {
    if (tolower(static_cast<unsigned char>(*c1)) !=
        tolower(static_cast<unsigned char>(*c2))) {
      return false;
    }
  }
  return true;
}
