#pragma once

#include "office3ds/api/direct_api_client.hpp"

namespace office3ds::platform_3ds {

class HttpRuntime {
public:
  ~HttpRuntime();

  HttpRuntime(const HttpRuntime &) = delete;
  HttpRuntime &operator=(const HttpRuntime &) = delete;
  HttpRuntime() = default;

  [[nodiscard]] bool initialize();
  void shutdown() noexcept;
  [[nodiscard]] bool ready() const noexcept;

private:
  bool initialized_ = false;
  void *socket_buffer_ = nullptr;
};

class CurlHttpTransport final : public api::HttpTransport {
public:
  [[nodiscard]] std::optional<api::ApiResponse> perform(const api::HttpRequest &request) override;
};

} // namespace office3ds::platform_3ds
