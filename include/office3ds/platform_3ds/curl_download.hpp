#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace office3ds::platform_3ds {

struct CurlRequest {
  std::string url;
  std::vector<std::string> headers;
  std::string body;
  std::size_t maximum_response_size = 0;
  bool post = false;
  bool follow_redirects = false;
};

struct CurlResponse {
  long status_code = 0;
  std::string content_type;
};

bool performCurlRequest(const CurlRequest &request, std::string &body, CurlResponse &response);
bool performCurlRequest(const CurlRequest &request, std::vector<std::uint8_t> &body,
                        CurlResponse &response);

} // namespace office3ds::platform_3ds
