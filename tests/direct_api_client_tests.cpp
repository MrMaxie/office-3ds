#include "office3ds/api/direct_api_client.hpp"

#include <cassert>
#include <stdexcept>

namespace {

class RecordingTransport final : public office3ds::api::HttpTransport {
public:
  std::optional<office3ds::api::ApiResponse>
  perform(const office3ds::api::HttpRequest &request) override {
    last = request;
    return office3ds::api::ApiResponse{200, "{}"};
  }

  office3ds::api::HttpRequest last;
};

} // namespace

int main() {
  RecordingTransport transport;
  office3ds::api::DirectApiClient client(transport, "https://api.example.test", "demo-token");
  office3ds::api::ApiRequest request{
    "GET", "/v1/items", {{"date", "2026-01-05"}, {"q", "A B"}}, {{"Accept", "application/json"}},
    {},    4096};
  const auto response = client.execute(request);
  assert(response.has_value());
  assert(transport.last.url == "https://api.example.test/v1/items?date=2026-01-05&q=A%20B");
  assert(transport.last.headers.size() == 2);
  assert(transport.last.headers.back().first == "Authorization");
  assert(transport.last.headers.back().second == "Bearer demo-token");
  assert(transport.last.max_response_size == 4096);

  bool rejected = false;
  try {
    request.path = "/../private";
    const auto ignored = client.execute(request);
    (void)ignored;
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);

  rejected = false;
  try {
    request.path = "/v1/items";
    request.headers = {{"authorization", "Bearer supplied-by-adapter"}};
    const auto ignored = client.execute(request);
    (void)ignored;
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);

  rejected = false;
  try {
    office3ds::api::DirectApiClient invalid(transport, "https://api.example.test", "");
    (void)invalid;
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  return 0;
}
