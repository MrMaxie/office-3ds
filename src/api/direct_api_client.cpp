#include "office3ds/api/direct_api_client.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace office3ds::api {
namespace {

bool contains_crlf(std::string_view value) {
  return value.find('\r') != std::string_view::npos || value.find('\n') != std::string_view::npos;
}

bool unsafe_path(std::string_view path) {
  if (path.empty() || path.front() != '/' || path.rfind("//", 0) == 0 ||
      path.find('?') != std::string_view::npos || path.find('#') != std::string_view::npos ||
      path.find("\\") != std::string_view::npos) {
    return true;
  }
  std::size_t start = 1;
  while (start <= path.size()) {
    const auto end = path.find('/', start);
    const auto segment =
      path.substr(start, end == std::string_view::npos ? path.size() - start : end - start);
    if (segment == "." || segment == "..") {
      return true;
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return false;
}

std::string url_encode(std::string_view value) {
  std::ostringstream output;
  output << std::uppercase << std::hex;
  for (const unsigned char character : value) {
    if (std::isalnum(character) != 0 || character == '-' || character == '_' || character == '.' ||
        character == '~') {
      output << static_cast<char>(character);
    } else {
      output << '%' << std::setw(2) << std::setfill('0') << static_cast<unsigned>(character);
    }
  }
  return output.str();
}

} // namespace

DirectApiClient::DirectApiClient(HttpTransport &transport, std::string base_url,
                                 std::string bearer_token)
    : transport_(transport), base_url_(std::move(base_url)),
      bearer_token_(std::move(bearer_token)) {
  if (base_url_.empty() || bearer_token_.empty() || base_url_.back() == '/' ||
      contains_crlf(base_url_) || contains_crlf(bearer_token_) ||
      base_url_.find('?') != std::string::npos || base_url_.find('#') != std::string::npos ||
      (base_url_.rfind("https://", 0) != 0 && base_url_.rfind("http://", 0) != 0)) {
    throw std::invalid_argument("API base URL or bearer credential is invalid");
  }
}

std::optional<ApiResponse> DirectApiClient::execute(const ApiRequest &request) const {
  if (unsafe_path(request.path) || request.max_response_size == 0) {
    throw std::invalid_argument("API request path or response limit is invalid");
  }

  HttpRequest outgoing{request.method, base_url_ + request.path, request.headers, request.body,
                       request.max_response_size};
  for (const auto &[name, value] : outgoing.headers) {
    if (name.empty() || contains_crlf(name) || contains_crlf(value)) {
      throw std::invalid_argument("API request contains an invalid header");
    }
    std::string lower;
    lower.reserve(name.size());
    std::transform(
      name.begin(), name.end(), std::back_inserter(lower),
      [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (lower == "authorization") {
      throw std::invalid_argument("product adapters cannot supply authorization headers");
    }
  }
  if (!request.query.empty()) {
    outgoing.url.push_back('?');
    for (std::size_t index = 0; index < request.query.size(); ++index) {
      if (index != 0) {
        outgoing.url.push_back('&');
      }
      outgoing.url += url_encode(request.query[index].first);
      outgoing.url.push_back('=');
      outgoing.url += url_encode(request.query[index].second);
    }
  }
  outgoing.headers.emplace_back("Authorization", "Bearer " + bearer_token_);
  return transport_.perform(outgoing);
}

RequestExecutor DirectApiClient::executor() const {
  return [this](const ApiRequest &request) { return execute(request); };
}

} // namespace office3ds::api
