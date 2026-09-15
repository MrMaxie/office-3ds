#pragma once

#include "office3ds/api/models.hpp"
#include "office3ds/api/version.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace office3ds::api {

struct ApiRequest {
  std::string method;
  std::string path;
  std::vector<std::pair<std::string, std::string>> query;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  std::size_t max_response_size = 256 * 1024;
};

struct ApiResponse {
  std::uint16_t status = 0;
  std::string body;
};

using RequestExecutor = std::function<std::optional<ApiResponse>(const ApiRequest &request)>;

class DashboardLoad {
public:
  virtual ~DashboardLoad() = default;
  [[nodiscard]] virtual DashboardLoadUpdate advance(const RequestExecutor &execute) = 0;
};

class Adapter {
public:
  virtual ~Adapter() = default;

  [[nodiscard]] virtual std::unique_ptr<DashboardLoad>
  begin_dashboard_load(std::string_view today) = 0;
  [[nodiscard]] virtual RecognitionResult send_recognition(const RecognitionRequest &request,
                                                           const RequestExecutor &execute) = 0;
};

using AdapterFactory = std::unique_ptr<Adapter> (*)();

} // namespace office3ds::api
