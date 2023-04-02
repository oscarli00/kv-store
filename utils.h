#ifndef UTILS_H
#define UTILS_H

#include <string>

void die(const std::string &msg);

void set_fd_nonblocking(int fd);

bool strn_equals(std::string s1, std::string s2);

#endif