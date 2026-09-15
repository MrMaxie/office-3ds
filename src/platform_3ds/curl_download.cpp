#include "office3ds/platform_3ds/curl_download.hpp"

#include <algorithm>
#include <memory>
#include <sys/select.h>
#include <utility>

#include <curl/curl.h>

namespace office3ds::platform_3ds {
namespace {

constexpr auto kConnectTimeoutMilliseconds = 10'000L;
constexpr auto kRequestTimeoutMilliseconds = 30'000L;
constexpr auto kMaximumRedirects = 4L;
constexpr auto kCertificateBundlePath = "romfs:/https-trust-roots.pem";

struct WriteTarget {
  void *body = nullptr;
  std::size_t maximum_size = 0;
  std::size_t size = 0;
  bool overflowed = false;
  void (*append)(void *, const std::uint8_t *, std::size_t) = nullptr;
};

template <typename Container>
void appendBytes(void *body, const std::uint8_t *data, std::size_t size) {
  auto &output = *static_cast<Container *>(body);
  output.insert(output.end(), data, data + size);
}

std::size_t writeResponse(char *data, std::size_t element_size, std::size_t element_count,
                          void *context) {
  auto &target = *static_cast<WriteTarget *>(context);
  if (element_size != 0 && element_count > SIZE_MAX / element_size) {
    target.overflowed = true;
    return 0;
  }
  const auto size = element_size * element_count;
  if (size > target.maximum_size - target.size) {
    target.overflowed = true;
    return 0;
  }
  target.append(target.body, reinterpret_cast<const std::uint8_t *>(data), size);
  target.size += size;
  return size;
}

template <typename Container>
bool perform(const CurlRequest &request, Container &body, CurlResponse &response) {
  body.clear();
  response = {};
  if (request.maximum_response_size == 0 || request.url.rfind("https://", 0) != 0) {
    return false;
  }

  const auto easy =
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>(curl_easy_init(), curl_easy_cleanup);
  if (!easy) {
    return false;
  }

  curl_slist *raw_headers = nullptr;
  for (const auto &header : request.headers) {
    auto *next = curl_slist_append(raw_headers, header.c_str());
    if (next == nullptr) {
      curl_slist_free_all(raw_headers);
      return false;
    }
    raw_headers = next;
  }
  const auto headers =
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>(raw_headers, curl_slist_free_all);
  WriteTarget target{&body, request.maximum_response_size, 0, false, appendBytes<Container>};

  const auto set = [&easy](CURLoption option, auto value) {
    return curl_easy_setopt(easy.get(), option, value);
  };
  CURLcode configured = CURLE_OK;
  const auto setIfOk = [&configured, &set](CURLoption option, auto value) {
    if (configured == CURLE_OK) {
      configured = set(option, value);
    }
  };
  setIfOk(CURLOPT_URL, request.url.c_str());
  setIfOk(CURLOPT_HTTPHEADER, headers.get());
  setIfOk(CURLOPT_WRITEFUNCTION, writeResponse);
  setIfOk(CURLOPT_WRITEDATA, &target);
  setIfOk(CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMilliseconds);
  setIfOk(CURLOPT_TIMEOUT_MS, kRequestTimeoutMilliseconds);
  setIfOk(CURLOPT_NOSIGNAL, 1L);
  setIfOk(CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
  setIfOk(CURLOPT_PROTOCOLS_STR, "https");
  setIfOk(CURLOPT_REDIR_PROTOCOLS_STR, "https");
  setIfOk(CURLOPT_FOLLOWLOCATION, request.follow_redirects ? 1L : 0L);
  setIfOk(CURLOPT_MAXREDIRS, kMaximumRedirects);
  setIfOk(CURLOPT_ACCEPT_ENCODING, "");
  setIfOk(CURLOPT_SSL_VERIFYPEER, 1L);
  setIfOk(CURLOPT_SSL_VERIFYHOST, 2L);
  setIfOk(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
  setIfOk(CURLOPT_CAINFO, kCertificateBundlePath);
  if (request.post) {
    setIfOk(CURLOPT_POST, 1L);
    setIfOk(CURLOPT_POSTFIELDS, request.body.data());
    setIfOk(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
  }
  if (configured != CURLE_OK) {
    return false;
  }

  const auto result = curl_easy_perform(easy.get());
  if (result != CURLE_OK || target.overflowed) {
    body.clear();
    return false;
  }

  (void)curl_easy_getinfo(easy.get(), CURLINFO_RESPONSE_CODE, &response.status_code);
  char *content_type = nullptr;
  (void)curl_easy_getinfo(easy.get(), CURLINFO_CONTENT_TYPE, &content_type);
  if (content_type != nullptr) {
    response.content_type = content_type;
  }
  return true;
}

} // namespace

bool performCurlRequest(const CurlRequest &request, std::string &body, CurlResponse &response) {
  return perform(request, body, response);
}

bool performCurlRequest(const CurlRequest &request, std::vector<std::uint8_t> &body,
                        CurlResponse &response) {
  return perform(request, body, response);
}

} // namespace office3ds::platform_3ds
