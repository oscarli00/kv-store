#ifndef UTILS_H
#define UTILS_H

#include <string>

void die(const std::string &msg);

void set_fd_nonblocking(int fd);

#endif