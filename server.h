#ifndef SERVER_H
#define SERVER_H

#include <memory>
#include <sys/epoll.h>
#include <vector>

#include "constants.h"

struct Status {
  bool req{false};
  bool res{false};
};

struct Conn {
  int fd{-1};

  // Stores info of what events to poll for
  Status state{};

  // Read buffer
  // Stores the data of client requests
  uint8_t rbuf[constants::READ_BUFFER_SIZE];
  size_t rbuf_size{};
  size_t rbuf_read{};

  // Write buffer
  // Stores the data for responses
  uint8_t wbuf[constants::WRITE_BUFFER_SIZE];
  size_t wbuf_size{};
  size_t wbuf_sent{};
};

class Server {
public:
  Server();
  ~Server();

  void loop();

private:
  // File descriptors
  int listen_fd_{};
  int epoll_fd_{};

  // Maps fd value to the respective Conn object
  std::vector<std::unique_ptr<Conn>> conn_map_{};
  epoll_event *events_{}; // Maybe change to unique_ptr

  void init();

  void create_socket();
  void bind_socket();
  void start_listen();
  void setup_epoll();

  void accept_conn();
  void read_into_buffer(Conn &conn);
  void parse_request(Conn &conn);
  int create_response(Conn &conn, uint32_t len);
  void write_from_buffer(Conn &conn);

  void check_err(Conn &conn, const std::string &op);
};

#endif