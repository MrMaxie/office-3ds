#include "office3ds/generated_adapter/demo_json_codec.hpp"
#include "office3ds/generated_adapter/generated_adapter.hpp"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace {

using office3ds::api::AdapterResult;
using office3ds::api::ApiRequest;
using office3ds::api::ApiResponse;
using office3ds::api::DashboardLoadStage;
using office3ds::generated_adapter::DeclarativeOperations;
using office3ds::generated_adapter::DemoJsonCodec;

DeclarativeOperations operations() {
  return {
    {"GET", "/v1/profile", "null", "null", "null",
     R"({"_kind":"object","id":{"1":"id","_kind":"field"},"display_name":{"1":"display_name","_kind":"field"},"role":{"1":{"1":"role","_kind":"field"},"2":"","_kind":"optional"},"avatar_url":{"1":{"1":"avatar_url","_kind":"field"},"2":"","_kind":"optional"}})"},
    {"GET", "/v1/worklog", R"({"week":{"1":{"1":"date","_kind":"context"},"_kind":"url_encode"}})",
     "null", "null",
     R"({"1":{"_kind":"object","date":{"1":"date","_kind":"field"},"minutes":{"1":"minutes","_kind":"field"},"expected_minutes":{"1":{"1":"expected_minutes","_kind":"field"},"2":0,"_kind":"default"}},"2":{"1":"items","_kind":"field"},"_kind":"list"})"},
    {"GET", "/v1/absences", "null", "null", "null",
     R"({"1":{"_kind":"object","id":{"1":"id","_kind":"field"},"person_name":{"1":"person_name","_kind":"field"},"starts_on":{"1":"starts_on","_kind":"field"},"ends_on":{"1":"ends_on","_kind":"field"},"return_date":{"1":{"1":"return_date","_kind":"field"},"2":"","_kind":"optional"},"label":{"1":"label","_kind":"field"},"group":{"1":{"1":"group","_kind":"field"},"2":"","_kind":"optional"}},"2":{"1":"items","_kind":"field"},"_kind":"list"})"},
    {"GET", "/v1/activity", "null", "null", "null",
     R"({"1":{"_kind":"object","id":{"1":{"1":"id","_kind":"field"},"_kind":"string"},"recipient_id":{"1":"recipient_id","_kind":"field"},"recipient_name":{"1":"recipient_name","_kind":"field"},"occurred_at":{"1":"occurred_at","_kind":"field"},"age_label":{"1":{"1":"age_label","_kind":"field"},"2":"","_kind":"optional"},"summary":{"1":"summary","_kind":"field"},"allowed_recognition_values":{"1":"allowed_recognition_values","_kind":"field"}},"2":{"1":"items","_kind":"field"},"_kind":"list"})"},
    {"POST", "/v1/recognitions", "null", R"({"Content-Type":"application/json"})",
     R"({"_kind":"object","source_event_id":{"1":{"1":"activity_id","_kind":"context"},"_kind":"number"},"recipient_id":{"1":"recipient_id","_kind":"context"},"value":{"1":"value","_kind":"context"},"comment":{"1":"message","_kind":"context"},"request_id":{"1":"request_id","_kind":"context"}})",
     R"({"_kind":"object","accepted":{"1":{"1":"accepted","_kind":"field"},"2":true,"_kind":"default"},"recognition_id":{"1":{"1":"id","_kind":"field"},"_kind":"string"}})"},
  };
}

void test_codec_rejects_malformed_documents() {
  assert(!DemoJsonCodec::decode_profile("{"));
  assert(!DemoJsonCodec::decode_profile(R"({"id":"member"})"));
  assert(!DemoJsonCodec::decode_worklog(R"([])"));
  assert(!DemoJsonCodec::decode_worklog(R"({"items":[{"date":"2026-06-17","minutes":1500}]})"));

  const office3ds::api::Profile profile{"alex-rivera", "Alex Rivera", {}, {}};
  assert(!DemoJsonCodec::decode_absences(
    R"({"items":[{"starts_on":"2026-07-01","ends_on":"2026-07-02"}]})", profile));
  assert(!DemoJsonCodec::decode_activity(
    R"({"items":[{"id":1,"recipient_id":"alex-rivera","recipient_name":"Alex Rivera","occurred_at":"2026-06-17T12:00:00Z","summary":"Reviewed","allowed_recognition_values":[2]}]})",
    profile));
}

void test_sequential_dashboard_load() {
  auto adapter = office3ds::generated_adapter::create_declarative_adapter(operations());
  assert(adapter);
  auto load = adapter->begin_dashboard_load("2026-06-17");

  std::vector<ApiRequest> requests;
  const std::vector<ApiResponse> responses = {
    {200,
     R"({"id":"alex-rivera","display_name":"Alex Rivera","role":"Workspace coordinator","avatar_url":"https://images.example.test/avatar.png"})"},
    {200, R"({"items":[{"date":"2026-06-17","minutes":420,"expected_minutes":480}]})"},
    {200,
     R"({"items":[{"id":"absence-1","person_name":"Alex Rivera","starts_on":"2026-07-06","ends_on":"2026-07-10","return_date":"2026-07-13","label":"time_off","group":"approved"}]})"},
    {200,
     R"({"items":[{"id":1,"recipient_id":"alex-rivera","recipient_name":"Alex Rivera","occurred_at":"2026-06-17T15:30:00Z","age_label":"today","summary":"Worklog was updated","allowed_recognition_values":[5,3,1]}]})"}};
  std::size_t response_index = 0;
  const auto execute = [&](const ApiRequest &request) -> std::optional<ApiResponse> {
    requests.push_back(request);
    return responses.at(response_index++);
  };

  auto update = load->advance(execute);
  assert(update.stage == DashboardLoadStage::worklog);
  assert(update.completed == 1 && !update.finished && !update.snapshot);
  update = load->advance(execute);
  assert(update.stage == DashboardLoadStage::absences);
  assert(update.completed == 2 && !update.finished);
  update = load->advance(execute);
  assert(update.stage == DashboardLoadStage::activity);
  assert(update.completed == 3 && !update.finished);
  update = load->advance(execute);
  assert(update.stage == DashboardLoadStage::complete);
  assert(update.completed == 4 && update.finished && update.result == AdapterResult::ok);
  assert(update.snapshot);
  assert(update.snapshot->profile.display_name == "Alex Rivera");
  assert(update.snapshot->worklog.size() == 1);
  assert(update.snapshot->worklog.front().expected_minutes == 480);
  assert(update.snapshot->absences.front().return_date == "2026-07-13");
  assert(update.snapshot->activity.front().recipient_id == "alex-rivera");
  assert(update.snapshot->activity.front().allowed_recognition_values ==
         std::vector<std::int32_t>({5, 3, 1}));

  assert(requests.size() == 4);
  assert(requests[0].path == "/v1/profile");
  assert(requests[1].path == "/v1/worklog");
  const std::vector<std::pair<std::string, std::string>> expected_query = {{"week", "2026-06-17"}};
  assert(requests[1].query == expected_query);
  assert(requests[3].path == "/v1/activity");

  const auto repeated = load->advance(execute);
  assert(repeated.finished && repeated.snapshot);
  assert(requests.size() == 4);
}

void test_dashboard_errors_stop_the_sequence() {
  auto adapter = office3ds::generated_adapter::create_declarative_adapter(operations());
  auto load = adapter->begin_dashboard_load("2026-06-17");
  auto update = load->advance([](const ApiRequest &) {
    return std::optional<ApiResponse>(ApiResponse{401, "{}"});
  });
  assert(update.finished && update.result == AdapterResult::unauthorized);
  assert(update.completed == 0 && !update.snapshot);

  load = adapter->begin_dashboard_load("2026-06-17");
  update = load->advance([](const ApiRequest &) {
    return std::optional<ApiResponse>(ApiResponse{200, "{}"});
  });
  assert(update.finished && update.result == AdapterResult::invalid_response);

  load = adapter->begin_dashboard_load("2026-06-17");
  update =
    load->advance([](const ApiRequest &) -> std::optional<ApiResponse> { return std::nullopt; });
  assert(update.finished && update.result == AdapterResult::unavailable);
}

void test_coalesce_selector_uses_the_first_available_value() {
  auto configured = operations();
  configured.profile.response =
    R"({"_kind":"object","id":{"1":"id","_kind":"field"},"display_name":{"1":{"1":"preferred_name","_kind":"field"},"2":{"1":"display_name","_kind":"field"},"_kind":"coalesce"}})";
  auto adapter = office3ds::generated_adapter::create_declarative_adapter(std::move(configured));
  auto load = adapter->begin_dashboard_load("2026-06-17");
  const auto update = load->advance([](const ApiRequest &) {
    return std::optional<ApiResponse>(
      ApiResponse{200, R"({"id":"alex-rivera","display_name":"Alex Rivera"})"});
  });
  assert(update.result == AdapterResult::ok && update.stage == DashboardLoadStage::worklog);
}

void test_recognition_request_and_response() {
  auto adapter = office3ds::generated_adapter::create_declarative_adapter(operations());
  ApiRequest captured;
  const auto result =
    adapter->send_recognition({"1", "alex-rivera", 5, "Careful review.", "request-001"},
                              [&](const ApiRequest &request) -> std::optional<ApiResponse> {
                                captured = request;
                                return ApiResponse{201, R"({"id":42,"request_id":"request-001"})"};
                              });
  assert(result.result == AdapterResult::ok && result.accepted);
  assert(result.recognition_id == "42");
  assert(captured.method == "POST" && captured.path == "/v1/recognitions");
  assert(captured.max_response_size == 16 * 1024);
  const auto body = nlohmann::json::parse(captured.body);
  assert(body.at("source_event_id") == 1);
  assert(body.at("recipient_id") == "alex-rivera");
  assert(body.at("value") == 5);
  assert(body.at("comment") == "Careful review.");
  assert(body.at("request_id") == "request-001");

  const auto invalid =
    adapter->send_recognition({"1", "alex-rivera", 2, {}, "request-002"},
                              [](const ApiRequest &) -> std::optional<ApiResponse> {
                                return ApiResponse{201, "{}"};
                              });
  assert(invalid.result == AdapterResult::invalid_response && !invalid.accepted);

  const auto invalid_number =
    adapter->send_recognition({"nan", "alex-rivera", 5, {}, "request-002"}, [](const ApiRequest &) {
      assert(false);
      return std::optional<ApiResponse>{};
    });
  assert(invalid_number.result == AdapterResult::invalid_response && !invalid_number.accepted);

  const auto unauthorized =
    adapter->send_recognition({"1", "alex-rivera", 3, {}, "request-003"},
                              [](const ApiRequest &) -> std::optional<ApiResponse> {
                                return ApiResponse{403, "{}"};
                              });
  assert(unauthorized.result == AdapterResult::unauthorized && !unauthorized.accepted);
}

void test_invalid_operation_configuration() {
  auto invalid = operations();
  invalid.recognition.method = "GET";
  assert(!office3ds::generated_adapter::create_declarative_adapter(std::move(invalid)));
}

} // namespace

int main() {
  test_codec_rejects_malformed_documents();
  test_sequential_dashboard_load();
  test_dashboard_errors_stop_the_sequence();
  test_coalesce_selector_uses_the_first_available_value();
  test_recognition_request_and_response();
  test_invalid_operation_configuration();
  return 0;
}
