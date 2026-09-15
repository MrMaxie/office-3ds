#include "office3ds/api/adapter.hpp"

#include <cassert>
#include <memory>
#include <optional>

namespace office3ds::generated {
std::unique_ptr<api::Adapter> create_product_adapter();
}

int main() {
  auto adapter = office3ds::generated::create_product_adapter();
  assert(adapter);
  auto load = adapter->begin_dashboard_load("2026-06-17");
  const auto update = load->advance(
    [](const office3ds::api::ApiRequest &request) -> std::optional<office3ds::api::ApiResponse> {
      assert(request.method == "GET");
      assert(request.path == "/v1/profile");
      return office3ds::api::ApiResponse{
        200, R"({"id":"alex-rivera","display_name":"Alex Rivera","role":"Coordinator"})"};
    });
  assert(update.result == office3ds::api::AdapterResult::ok);
  assert(update.stage == office3ds::api::DashboardLoadStage::worklog);
  assert(!update.finished && update.completed == 1);
  return 0;
}
