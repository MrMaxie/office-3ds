#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace office3ds::demo {

struct ServerConfig {
  std::string database_path;
  std::string bind_address{"127.0.0.1"};
  std::uint16_t port{8080};
};

class DemoHttpServer {
public:
  explicit DemoHttpServer(ServerConfig config);
  ~DemoHttpServer();

  DemoHttpServer(const DemoHttpServer &) = delete;
  DemoHttpServer &operator=(const DemoHttpServer &) = delete;

  // Binds once and returns the actual port, including for port 0. Returns 0
  // when the socket cannot be bound.
  [[nodiscard]] std::uint16_t bind();
  [[nodiscard]] bool listen();
  void waitUntilReady() const;
  void stop();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Blocks until the HTTP server is stopped. Returns false when the socket
// cannot be bound.
bool runServer(const ServerConfig &config);

} // namespace office3ds::demo
