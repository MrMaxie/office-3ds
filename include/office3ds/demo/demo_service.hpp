#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace office3ds::demo {

struct ServiceConfig {
  std::string database_path;
};

struct ApiResponse {
  int status{500};
  std::string content_type{"application/json"};
  std::string body;
};

class DemoService {
public:
  explicit DemoService(ServiceConfig config);
  ~DemoService();

  DemoService(const DemoService &) = delete;
  DemoService &operator=(const DemoService &) = delete;
  DemoService(DemoService &&) noexcept;
  DemoService &operator=(DemoService &&) noexcept;

  void migrateAndSeed();
  void reset();

  // Writes the only copy of the credential to output_path. The token value is
  // never returned to the caller or written to application logs.
  void issueBearerToken(const std::string &output_path, std::chrono::seconds lifetime);

  ApiResponse handle(std::string_view method, std::string_view path, std::string_view authorization,
                     std::string_view request_body, std::int64_t now_epoch_seconds = 0);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace office3ds::demo
