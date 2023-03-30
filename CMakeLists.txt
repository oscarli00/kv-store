cmake_minimum_required(VERSION 3.22)
project(redis)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-Wall -Wextra -O2 -g")

add_executable(server server.cpp utils.cpp)
add_executable(client client.cpp utils.cpp)