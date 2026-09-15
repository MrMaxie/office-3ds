#include "office3ds/platform_3ds/http.hpp"

#include "office3ds/bridge/pairing.hpp"

#include <cstdlib>
#include <limits>
#include <malloc.h>
#include <memory>
#include <sys/select.h>

#include <3ds.h>
#include <curl/curl.h>

namespace office3ds::platform_3ds {
namespace {

constexpr std::size_t kSocketBufferAlignment = 0x1000U;
constexpr std::size_t kSocketBufferSize = 0x100000U;
constexpr long kConnectTimeoutMilliseconds = 10'000L;
constexpr long kRequestTimeoutMilliseconds = 30'000L;

struct WriteTarget {
  std::string *body = nullptr;
  std::size_t maximum_size = 0;
  bool overflowed = false;
};

std::size_t write_response(char *data, std::size_t element_size, std::size_t element_count,
                           void *context) {
  auto &target = *static_cast<WriteTarget *>(context);
  if (element_size != 0 && element_count > std::numeric_limits<std::size_t>::max() / element_size) {
    target.overflowed = true;
    return 0;
  }
  const auto size = element_size * element_count;
  if (size > target.maximum_size - target.body->size()) {
    target.overflowed = true;
    return 0;
  }
  target.body->append(data, size);
  return size;
}

bool valid_url(std::string_view url) {
  if (url.rfind("https://", 0) == 0) {
    return true;
  }
  if (url.rfind("http://", 0) != 0) {
    return false;
  }
  const auto host_end = url.find_first_of(":/", 7);
  const auto host =
    url.substr(7, host_end == std::string_view::npos ? url.size() - 7 : host_end - 7);
  return bridge::is_local_bridge_ipv4(host);
}

} // namespace

HttpRuntime::~HttpRuntime() { shutdown(); }

bool HttpRuntime::initialize() {
  if (initialized_) {
    return true;
  }
  socket_buffer_ = memalign(kSocketBufferAlignment, kSocketBufferSize);
  if (socket_buffer_ == nullptr) {
    return false;
  }
  if (R_FAILED(socInit(static_cast<u32 *>(socket_buffer_), kSocketBufferSize))) {
    std::free(socket_buffer_);
    socket_buffer_ = nullptr;
    return false;
  }
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
    socExit();
    std::free(socket_buffer_);
    socket_buffer_ = nullptr;
    return false;
  }
  if (R_FAILED(httpcInit(0x1000U))) {
    curl_global_cleanup();
    socExit();
    std::free(socket_buffer_);
    socket_buffer_ = nullptr;
    return false;
  }
  initialized_ = true;
  return true;
}

void HttpRuntime::shutdown() noexcept {
  if (!initialized_) {
    return;
  }
  httpcExit();
  curl_global_cleanup();
  socExit();
  std::free(socket_buffer_);
  socket_buffer_ = nullptr;
  initialized_ = false;
}

bool HttpRuntime::ready() const noexcept { return initialized_; }

std::optional<api::ApiResponse> CurlHttpTransport::perform(const api::HttpRequest &request) {
  if (!valid_url(request.url) || request.max_response_size == 0 ||
      (request.method != "GET" && request.method != "POST")) {
    return std::nullopt;
  }
  auto easy =
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>(curl_easy_init(), curl_easy_cleanup);
  if (!easy) {
    return std::nullopt;
  }

  curl_slist *raw_headers = nullptr;
  for (const auto &[name, value] : request.headers) {
    const auto header = name + ": " + value;
    auto *next = curl_slist_append(raw_headers, header.c_str());
    if (next == nullptr) {
      curl_slist_free_all(raw_headers);
      return std::nullopt;
    }
    raw_headers = next;
  }
  auto headers =
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>(raw_headers, curl_slist_free_all);
  std::string body;
  WriteTarget target{&body, request.max_response_size, false};

  const auto set = [&easy](CURLoption option, auto value) {
    return curl_easy_setopt(easy.get(), option, value);
  };
  CURLcode configured = CURLE_OK;
  const auto set_if_ok = [&configured, &set](CURLoption option, auto value) {
    if (configured == CURLE_OK) {
      configured = set(option, value);
    }
  };
  set_if_ok(CURLOPT_URL, request.url.c_str());
  set_if_ok(CURLOPT_HTTPHEADER, headers.get());
  set_if_ok(CURLOPT_WRITEFUNCTION, write_response);
  set_if_ok(CURLOPT_WRITEDATA, &target);
  set_if_ok(CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMilliseconds);
  set_if_ok(CURLOPT_TIMEOUT_MS, kRequestTimeoutMilliseconds);
  set_if_ok(CURLOPT_NOSIGNAL, 1L);
  set_if_ok(CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
  set_if_ok(CURLOPT_PROTOCOLS_STR, "http,https");
  set_if_ok(CURLOPT_REDIR_PROTOCOLS_STR, "https");
  set_if_ok(CURLOPT_FOLLOWLOCATION, 0L);
  set_if_ok(CURLOPT_ACCEPT_ENCODING, "");
  set_if_ok(CURLOPT_SSL_VERIFYPEER, 1L);
  set_if_ok(CURLOPT_SSL_VERIFYHOST, 2L);
  set_if_ok(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
  if (request.method == "POST") {
    set_if_ok(CURLOPT_POST, 1L);
    set_if_ok(CURLOPT_POSTFIELDS, request.body.data());
    set_if_ok(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
  }
  if (configured != CURLE_OK || curl_easy_perform(easy.get()) != CURLE_OK || target.overflowed) {
    return std::nullopt;
  }
  long status = 0;
  if (curl_easy_getinfo(easy.get(), CURLINFO_RESPONSE_CODE, &status) != CURLE_OK || status < 100 ||
      status > 599) {
    return std::nullopt;
  }
  return api::ApiResponse{static_cast<std::uint16_t>(status), std::move(body)};
}

} // namespace office3ds::platform_3ds
