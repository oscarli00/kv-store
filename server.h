#ifndef SERVER_H
#define SERVER_H

#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <unordered_map>
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
  Server(in_addr_t ip, in_port_t port);
  ~Server();

  void loop();

private:
  in_addr_t ip_;
  in_port_t port_;

  // File descriptors
  int listen_fd_{};
  int epoll_fd_{};

  // Maps fd value to the respective Conn object
  std::vector<std::unique_ptr<Conn>> conn_map_{};

  // Epoll events buffer
  epoll_event *events_{}; // Maybe change to vector

  std::unordered_map<std::string, std::string> db_{};

  void init();

  void create_socket();
  void bind_socket();
  void start_listen();
  void setup_epoll();

  void accept_conn();
  void read_into_buffer(Conn &conn);
  void read_request(Conn &conn);

  int do_cmd(Conn &conn, const std::vector<std::string> &cmd);
  int cmd_get(Conn &conn, const std::string &key);
  int cmd_set(Conn &conn, const std::string &key, const std::string &val);
  int cmd_del(Conn &conn, const std::string &key);

  int create_response(Conn &conn, const std::string &str);
  void write_from_buffer(Conn &conn);

  void check_err(Conn &conn, const std::string &op);
};

#endif