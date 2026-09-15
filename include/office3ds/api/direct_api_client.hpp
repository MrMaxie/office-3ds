#pragma once

#include "office3ds/api/adapter.hpp"

#include <string>

namespace office3ds::api {

struct HttpRequest {
  std::string method;
  std::string url;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  std::size_t max_response_size = 256 * 1024;
};

class HttpTransport {
public:
  virtual ~HttpTransport() = default;
  [[nodiscard]] virtual std::optional<ApiResponse> perform(const HttpRequest &request) = 0;
};

class DirectApiClient {
public:
  DirectApiClient(HttpTransport &transport, std::string base_url, std::string bearer_token);

  [[nodiscard]] std::optional<ApiResponse> execute(const ApiRequest &request) const;
  [[nodiscard]] RequestExecutor executor() const;

private:
  HttpTransport &transport_;
  std::string base_url_;
  std::string bearer_token_;
};

} // namespace office3ds::api
