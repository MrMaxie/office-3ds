#include "office3ds/demo/demo_service.hpp"
#include "office3ds/generated_adapter/generated_adapter.hpp"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <string>

namespace {

class TemporaryDirectory {
public:
  TemporaryDirectory() : path_(make_path()) { std::filesystem::create_directories(path_); }
  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  const std::filesystem::path &path() const { return path_; }

private:
  static std::filesystem::path make_path() {
    std::random_device random;
    return std::filesystem::temp_directory_path() /
           ("office-3ds-generated-integration-" + std::to_string(random()));
  }

  std::filesystem::path path_;
};

std::string read_token(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  std::string value{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
    value.pop_back();
  }
  return nlohmann::json::parse(value).at("access_token").get<std::string>();
}

} // namespace

namespace office3ds::generated {
std::unique_ptr<api::Adapter> create_product_adapter();
}

int main() {
  TemporaryDirectory temporary;
  const auto database = temporary.path() / "demo.sqlite3";
  const auto credential_file = temporary.path() / "token.txt";
  office3ds::demo::DemoService service({database.string()});
  service.issueBearerToken(credential_file.string(), std::chrono::hours(1));
  const auto authorization = "Bearer " + read_token(credential_file);

  auto adapter = office3ds::generated::create_product_adapter();
  assert(adapter);

  const auto execute =
    [&](const office3ds::api::ApiRequest &request) -> std::optional<office3ds::api::ApiResponse> {
    const auto response = service.handle(request.method, request.path, authorization, request.body);
    return office3ds::api::ApiResponse{static_cast<std::uint16_t>(response.status), response.body};
  };

  auto load = adapter->begin_dashboard_load("2026-06-17");
  office3ds::api::DashboardLoadUpdate update;
  for (int operation = 0; operation < 4; ++operation) {
    update = load->advance(execute);
  }
  assert(update.finished && update.result == office3ds::api::AdapterResult::ok);
  assert(update.snapshot);
  assert(update.snapshot->worklog.size() == 3);
  assert(update.snapshot->absences.size() == 1);
  assert(update.snapshot->activity.size() == 2);
  assert(update.snapshot->activity.front().allowed_recognition_values ==
         std::vector<std::int32_t>({5, 3, 1}));

  const auto recognition = adapter->send_recognition(
    {update.snapshot->activity.front().id, update.snapshot->activity.front().recipient_id, 5,
     "Thank you for the review.", "integration-001"},
    execute);
  assert(recognition.result == office3ds::api::AdapterResult::ok);
  assert(recognition.accepted && !recognition.recognition_id.empty());
  return 0;
}
