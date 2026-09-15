#include "office3ds/generated_adapter/generated_adapter.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace office3ds::generated_adapter {
namespace {

using Json = nlohmann::json;
using Context = std::vector<std::pair<std::string, Json>>;

api::AdapterResult response_error(const std::optional<api::ApiResponse> &response) {
  if (!response) {
    return api::AdapterResult::unavailable;
  }
  if (response->status == 401 || response->status == 403) {
    return api::AdapterResult::unauthorized;
  }
  if (response->status < 200 || response->status >= 300) {
    return api::AdapterResult::unavailable;
  }
  return api::AdapterResult::ok;
}

std::optional<Json> parse_json(std::string_view input) {
  if (input.empty()) {
    return std::nullopt;
  }
  try {
    return Json::parse(input.begin(), input.end());
  } catch (const Json::exception &) {
    return std::nullopt;
  }
}

const Json *field_at(const Json &source, std::string_view path) {
  const Json *current = &source;
  std::size_t offset = 0;
  while (offset <= path.size()) {
    const auto separator = path.find('.', offset);
    const auto part = path.substr(offset, separator == std::string_view::npos ? path.size() - offset
                                                                              : separator - offset);
    if (part.empty() || !current->is_object()) {
      return nullptr;
    }
    const auto found = current->find(std::string(part));
    if (found == current->end()) {
      return nullptr;
    }
    current = &*found;
    if (separator == std::string_view::npos) {
      return current;
    }
    offset = separator + 1;
  }
  return nullptr;
}

const Json *context_value(const Context &context, std::string_view name) {
  const auto found = std::find_if(context.begin(), context.end(),
                                  [&](const auto &entry) { return entry.first == name; });
  return found == context.end() ? nullptr : &found->second;
}

std::optional<Json> evaluate(const Json &node, const Json &source, const Context &context,
                             std::size_t depth = 0);

std::optional<Json> evaluate_object(const Json &node, const Json &source, const Context &context,
                                    std::size_t depth) {
  Json result = Json::object();
  for (const auto &[key, child] : node.items()) {
    if (key == "_kind" ||
        (!key.empty() && std::all_of(key.begin(), key.end(), [](unsigned char character) {
          return character >= '0' && character <= '9';
        }))) {
      continue;
    }
    const auto value = evaluate(child, source, context, depth + 1);
    if (!value) {
      return std::nullopt;
    }
    result[key] = *value;
  }
  return result;
}

std::optional<Json> evaluate_list(const Json &node, const Json &source, const Context &context,
                                  std::size_t depth) {
  const Json *items = &source;
  const Json *mapping = &node;
  std::optional<Json> selected_items;
  const auto explicit_mapping = node.find("1");
  const auto explicit_source = node.find("2");
  if (explicit_mapping != node.end()) {
    mapping = &*explicit_mapping;
  }
  if (explicit_source != node.end()) {
    selected_items = evaluate(*explicit_source, source, context, depth + 1);
    if (!selected_items || !selected_items->is_array()) {
      return std::nullopt;
    }
    items = &*selected_items;
    mapping = explicit_mapping != node.end() ? &*explicit_mapping : nullptr;
  } else if (!source.is_array()) {
    const auto found = source.is_object() ? source.find("items") : source.end();
    if (found == source.end() || !found->is_array()) {
      return std::nullopt;
    }
    items = &*found;
  }
  if (mapping == nullptr || !items->is_array()) {
    return std::nullopt;
  }

  Json result = Json::array();
  for (const auto &item : *items) {
    std::optional<Json> mapped;
    if (mapping == &node) {
      mapped = evaluate_object(node, item, context, depth + 1);
    } else {
      mapped = evaluate(*mapping, item, context, depth + 1);
    }
    if (!mapped) {
      return std::nullopt;
    }
    result.push_back(std::move(*mapped));
  }
  return result;
}

std::optional<Json> evaluate_number(const Json &value) {
  if (value.is_number()) {
    return value;
  }
  if (!value.is_string()) {
    return std::nullopt;
  }
  const auto text = value.get<std::string>();
  std::int64_t integer = 0;
  const auto integer_result = std::from_chars(text.data(), text.data() + text.size(), integer);
  if (integer_result.ec == std::errc{} && integer_result.ptr == text.data() + text.size()) {
    return Json(integer);
  }
  try {
    std::size_t consumed = 0;
    const auto decimal = std::stod(text, &consumed);
    if (consumed == text.size() && std::isfinite(decimal)) {
      return Json(decimal);
    }
  } catch (const std::exception &) {
  }
  return std::nullopt;
}

std::optional<Json> evaluate(const Json &node, const Json &source, const Context &context,
                             std::size_t depth) {
  if (depth > 24) {
    return std::nullopt;
  }
  if (!node.is_object()) {
    return node;
  }
  const auto kind_entry = node.find("_kind");
  if (kind_entry == node.end()) {
    return evaluate_object(node, source, context, depth);
  }
  if (!kind_entry->is_string()) {
    return std::nullopt;
  }
  const auto kind = kind_entry->get<std::string>();
  if (kind == "object") {
    return evaluate_object(node, source, context, depth);
  }
  if (kind == "list") {
    return evaluate_list(node, source, context, depth);
  }
  if (kind == "field" || kind == "context") {
    const auto argument = node.find("1");
    if (argument == node.end() || !argument->is_string()) {
      return std::nullopt;
    }
    const auto name = argument->get<std::string>();
    const auto *value = kind == "field" ? field_at(source, name) : context_value(context, name);
    return value == nullptr ? std::nullopt : std::optional<Json>(*value);
  }
  if (kind == "optional" || kind == "default") {
    const auto argument = node.find("1");
    const auto fallback = node.find("2");
    if (argument == node.end()) {
      return std::nullopt;
    }
    const auto value = evaluate(*argument, source, context, depth + 1);
    if (value && !value->is_null()) {
      return value;
    }
    return fallback == node.end() ? std::optional<Json>(nullptr)
                                  : evaluate(*fallback, source, context, depth + 1);
  }
  if (kind == "coalesce") {
    for (std::size_t index = 1;; ++index) {
      const auto argument = node.find(std::to_string(index));
      if (argument == node.end()) {
        break;
      }
      const auto value = evaluate(*argument, source, context, depth + 1);
      if (value && !value->is_null()) {
        return value;
      }
    }
    return std::nullopt;
  }

  const auto argument = node.find("1");
  if (argument == node.end()) {
    return std::nullopt;
  }
  const auto value = evaluate(*argument, source, context, depth + 1);
  if (!value) {
    return std::nullopt;
  }
  if (kind == "url_encode") {
    return value;
  }
  if (kind == "string") {
    if (value->is_string()) {
      return value;
    }
    if (value->is_boolean()) {
      return Json(*value ? "true" : "false");
    }
    if (value->is_number_integer()) {
      return Json(std::to_string(value->get<std::int64_t>()));
    }
    if (value->is_number_unsigned()) {
      return Json(std::to_string(value->get<std::uint64_t>()));
    }
    if (value->is_number_float()) {
      return Json(std::to_string(value->get<double>()));
    }
    return std::nullopt;
  }
  if (kind == "number") {
    return evaluate_number(*value);
  }
  if (kind == "date") {
    if (!value->is_string()) {
      return std::nullopt;
    }
    const auto date = value->get<std::string>();
    const auto digit = [&](std::size_t index) { return date[index] >= '0' && date[index] <= '9'; };
    if (date.size() < 10 || date.size() > 35 || !digit(0) || !digit(1) || !digit(2) || !digit(3) ||
        date[4] != '-' || !digit(5) || !digit(6) || date[7] != '-' || !digit(8) || !digit(9)) {
      return std::nullopt;
    }
    return value;
  }
  return std::nullopt;
}

std::optional<std::string> scalar_string(const Json &value) {
  if (value.is_string()) {
    return value.get<std::string>();
  }
  if (value.is_boolean()) {
    return value.get<bool>() ? "true" : "false";
  }
  if (value.is_number_integer()) {
    return std::to_string(value.get<std::int64_t>());
  }
  if (value.is_number_unsigned()) {
    return std::to_string(value.get<std::uint64_t>());
  }
  if (value.is_number_float()) {
    return std::to_string(value.get<double>());
  }
  return std::nullopt;
}

bool append_descriptor_values(std::string_view descriptor, const Context &context,
                              std::vector<std::pair<std::string, std::string>> &destination) {
  const auto mapping = parse_json(descriptor);
  if (!mapping) {
    return false;
  }
  if (mapping->is_null()) {
    return true;
  }
  const auto value = evaluate(*mapping, Json::object(), context);
  if (!value || !value->is_object()) {
    return false;
  }
  for (const auto &[key, entry] : value->items()) {
    const auto text = scalar_string(entry);
    if (!text) {
      return false;
    }
    destination.emplace_back(key, *text);
  }
  return true;
}

std::optional<Json> map_response(std::string_view descriptor, std::string_view body) {
  const auto mapping = parse_json(descriptor);
  const auto document = parse_json(body);
  if (!mapping || !document) {
    return std::nullopt;
  }
  return evaluate(*mapping, *document, {});
}

std::optional<std::string> required_string(const Json &object, const char *key) {
  const auto found = object.find(key);
  if (found == object.end() || !found->is_string()) {
    return std::nullopt;
  }
  const auto value = found->get<std::string>();
  return value.empty() ? std::nullopt : std::optional<std::string>(value);
}

std::string optional_string(const Json &object, const char *key) {
  const auto found = object.find(key);
  return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

std::optional<std::int32_t> required_int32(const Json &object, const char *key) {
  const auto found = object.find(key);
  if (found == object.end() || !found->is_number_integer()) {
    return std::nullopt;
  }
  const auto value = found->get<std::int64_t>();
  if (value < std::numeric_limits<std::int32_t>::min() ||
      value > std::numeric_limits<std::int32_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::int32_t>(value);
}

std::optional<api::Profile> mapped_profile(const Json &value) {
  if (!value.is_object()) {
    return std::nullopt;
  }
  const auto id = required_string(value, "id");
  const auto display_name = required_string(value, "display_name");
  if (!id || !display_name) {
    return std::nullopt;
  }
  return api::Profile{*id, *display_name, optional_string(value, "role"),
                      optional_string(value, "avatar_url")};
}

std::optional<std::vector<api::WorklogDay>> mapped_worklog(const Json &value) {
  if (!value.is_array()) {
    return std::nullopt;
  }
  std::vector<api::WorklogDay> result;
  result.reserve(value.size());
  for (const auto &item : value) {
    const auto date = required_string(item, "date");
    const auto minutes = required_int32(item, "minutes");
    const auto expected = required_int32(item, "expected_minutes").value_or(0);
    if (!date || !minutes || *minutes < 0 || *minutes > 1440 || expected < 0 || expected > 1440) {
      return std::nullopt;
    }
    result.push_back({*date, *minutes, expected});
  }
  return result;
}

std::optional<std::vector<api::Absence>> mapped_absences(const Json &value) {
  if (!value.is_array()) {
    return std::nullopt;
  }
  std::vector<api::Absence> result;
  result.reserve(value.size());
  for (const auto &item : value) {
    const auto id = required_string(item, "id");
    const auto person = required_string(item, "person_name");
    const auto starts = required_string(item, "starts_on");
    const auto ends = required_string(item, "ends_on");
    const auto label = required_string(item, "label");
    if (!id || !person || !starts || !ends || !label) {
      return std::nullopt;
    }
    result.push_back({*id, *person, *starts, *ends, optional_string(item, "return_date"), *label,
                      optional_string(item, "group"), optional_string(item, "avatar_url")});
  }
  return result;
}

bool recognition_value(std::int32_t value) { return value == 5 || value == 3 || value == 1; }

std::optional<std::vector<api::ActivityEvent>> mapped_activity(const Json &value) {
  if (!value.is_array()) {
    return std::nullopt;
  }
  std::vector<api::ActivityEvent> result;
  result.reserve(value.size());
  for (const auto &item : value) {
    const auto id = required_string(item, "id");
    const auto recipient_id = required_string(item, "recipient_id");
    const auto recipient_name = required_string(item, "recipient_name");
    const auto occurred_at = required_string(item, "occurred_at");
    const auto summary = required_string(item, "summary");
    const auto allowed = item.find("allowed_recognition_values");
    if (!id || !recipient_id || !recipient_name || !occurred_at || !summary ||
        allowed == item.end() || !allowed->is_array()) {
      return std::nullopt;
    }
    std::vector<std::int32_t> allowed_values;
    for (const auto &entry : *allowed) {
      if (!entry.is_number_integer()) {
        return std::nullopt;
      }
      const auto number = entry.get<std::int32_t>();
      if (!recognition_value(number)) {
        return std::nullopt;
      }
      allowed_values.push_back(number);
    }
    if (allowed_values.empty()) {
      return std::nullopt;
    }
    result.push_back({*id, *recipient_id, *recipient_name, *occurred_at,
                      optional_string(item, "age_label"), *summary, std::move(allowed_values),
                      optional_string(item, "avatar_url")});
  }
  return result;
}

api::DashboardLoadStage stage_for_index(std::size_t index) {
  switch (index) {
  case 0:
    return api::DashboardLoadStage::profile;
  case 1:
    return api::DashboardLoadStage::worklog;
  case 2:
    return api::DashboardLoadStage::absences;
  case 3:
    return api::DashboardLoadStage::activity;
  default:
    return api::DashboardLoadStage::complete;
  }
}

class DeclarativeDashboardLoad final : public api::DashboardLoad {
public:
  DeclarativeDashboardLoad(DeclarativeOperations operations, std::string today)
      : operations_(std::move(operations)), today_(std::move(today)) {}

  api::DashboardLoadUpdate advance(const api::RequestExecutor &execute) override {
    if (finished_) {
      return update(api::DashboardLoadStage::complete, api::AdapterResult::ok, true, snapshot_);
    }
    const auto request = request_for_stage();
    if (!request) {
      finished_ = true;
      return update(stage_for_index(completed_), api::AdapterResult::invalid_response, true,
                    std::nullopt);
    }
    const auto response = execute(*request);
    const auto transport_result = response_error(response);
    if (transport_result != api::AdapterResult::ok) {
      finished_ = true;
      return update(stage_for_index(completed_), transport_result, true, std::nullopt);
    }
    if (!decode_stage(response->body)) {
      finished_ = true;
      return update(stage_for_index(completed_), api::AdapterResult::invalid_response, true,
                    std::nullopt);
    }
    ++completed_;
    if (completed_ == 4) {
      finished_ = true;
      return update(api::DashboardLoadStage::complete, api::AdapterResult::ok, true, snapshot_);
    }
    return update(stage_for_index(completed_), api::AdapterResult::ok, false, std::nullopt);
  }

private:
  api::DashboardLoadUpdate update(api::DashboardLoadStage stage, api::AdapterResult result,
                                  bool finished,
                                  std::optional<api::DashboardSnapshot> snapshot) const {
    return {stage, static_cast<std::uint8_t>(completed_), 4, finished, result, std::move(snapshot)};
  }

  const OperationDescriptor &operation_for_stage() const {
    switch (completed_) {
    case 0:
      return operations_.profile;
    case 1:
      return operations_.worklog;
    case 2:
      return operations_.absences;
    default:
      return operations_.activity;
    }
  }

  std::optional<api::ApiRequest> request_for_stage() const {
    const auto &operation = operation_for_stage();
    const Context context = {{"date", today_}};
    api::ApiRequest request{operation.method, operation.path, {}, {}, {}, 256 * 1024};
    if (!append_descriptor_values(operation.query, context, request.query) ||
        !append_descriptor_values(operation.headers, context, request.headers)) {
      return std::nullopt;
    }
    if (std::none_of(request.headers.begin(), request.headers.end(),
                     [](const auto &header) { return header.first == "Accept"; })) {
      request.headers.emplace_back("Accept", "application/json");
    }
    return request;
  }

  bool decode_stage(std::string_view body) {
    const auto mapped = map_response(operation_for_stage().response, body);
    if (!mapped) {
      return false;
    }
    switch (completed_) {
    case 0: {
      const auto value = mapped_profile(*mapped);
      if (!value) {
        return false;
      }
      snapshot_.profile = *value;
      return true;
    }
    case 1: {
      const auto value = mapped_worklog(*mapped);
      if (!value) {
        return false;
      }
      snapshot_.worklog = *value;
      return true;
    }
    case 2: {
      const auto value = mapped_absences(*mapped);
      if (!value) {
        return false;
      }
      snapshot_.absences = *value;
      return true;
    }
    default: {
      const auto value = mapped_activity(*mapped);
      if (!value) {
        return false;
      }
      snapshot_.activity = *value;
      return true;
    }
    }
  }

  DeclarativeOperations operations_;
  std::string today_;
  api::DashboardSnapshot snapshot_;
  std::size_t completed_ = 0;
  bool finished_ = false;
};

class DeclarativeAdapter final : public api::Adapter {
public:
  explicit DeclarativeAdapter(DeclarativeOperations operations)
      : operations_(std::move(operations)) {}

  std::unique_ptr<api::DashboardLoad> begin_dashboard_load(std::string_view today) override {
    return std::make_unique<DeclarativeDashboardLoad>(operations_, std::string(today));
  }

  api::RecognitionResult send_recognition(const api::RecognitionRequest &request,
                                          const api::RequestExecutor &execute) override {
    if (!recognition_value(request.value) || request.request_id.empty() ||
        request.request_id.size() > 64 || request.recipient_id.empty() ||
        request.recipient_id.size() > 64 || request.activity_id.empty() ||
        request.activity_id.size() > 64 || request.message.size() > 280) {
      return {api::AdapterResult::invalid_response,
              false,
              {},
              "Recognition request is outside the supported contract."};
    }
    const Context context = {{"activity_id", request.activity_id},
                             {"recipient_id", request.recipient_id},
                             {"value", request.value},
                             {"message", request.message},
                             {"request_id", request.request_id}};
    api::ApiRequest outgoing{
      operations_.recognition.method, operations_.recognition.path, {}, {}, {}, 16 * 1024};
    if (!append_descriptor_values(operations_.recognition.query, context, outgoing.query) ||
        !append_descriptor_values(operations_.recognition.headers, context, outgoing.headers)) {
      return {
        api::AdapterResult::invalid_response, false, {}, "Recognition request mapping is invalid."};
    }
    const auto body_mapping = parse_json(operations_.recognition.body);
    const auto body =
      body_mapping ? evaluate(*body_mapping, Json::object(), context) : std::nullopt;
    if (!body || !body->is_object()) {
      return {
        api::AdapterResult::invalid_response, false, {}, "Recognition request mapping is invalid."};
    }
    outgoing.body = body->dump();
    if (std::none_of(outgoing.headers.begin(), outgoing.headers.end(),
                     [](const auto &header) { return header.first == "Accept"; })) {
      outgoing.headers.emplace_back("Accept", "application/json");
    }

    const auto response = execute(outgoing);
    const auto result = response_error(response);
    if (result != api::AdapterResult::ok) {
      return {result, false, {}, "Recognition could not be submitted."};
    }
    const auto mapped = map_response(operations_.recognition.response, response->body);
    if (!mapped || !mapped->is_object()) {
      return {api::AdapterResult::invalid_response,
              false,
              {},
              "Recognition response did not match the product contract."};
    }
    const auto accepted = mapped->find("accepted");
    const auto recognition_id = required_string(*mapped, "recognition_id");
    if (accepted == mapped->end() || !accepted->is_boolean() || !recognition_id) {
      return {api::AdapterResult::invalid_response,
              false,
              {},
              "Recognition response did not match the product contract."};
    }
    return {api::AdapterResult::ok, accepted->get<bool>(), *recognition_id,
            optional_string(*mapped, "message")};
  }

private:
  DeclarativeOperations operations_;
};

bool valid_operation(const OperationDescriptor &operation, std::string_view expected_method,
                     bool requires_body = false) {
  if (operation.method != expected_method || operation.path.empty() ||
      operation.path.front() != '/' || operation.response.empty() ||
      (requires_body && operation.body.empty())) {
    return false;
  }
  return parse_json(operation.query).has_value() && parse_json(operation.headers).has_value() &&
         parse_json(operation.body).has_value() && parse_json(operation.response).has_value();
}

} // namespace

std::unique_ptr<api::Adapter> create_declarative_adapter(DeclarativeOperations operations) {
  if (!valid_operation(operations.profile, "GET") || !valid_operation(operations.worklog, "GET") ||
      !valid_operation(operations.absences, "GET") ||
      !valid_operation(operations.activity, "GET") ||
      !valid_operation(operations.recognition, "POST", true)) {
    return nullptr;
  }
  return std::make_unique<DeclarativeAdapter>(std::move(operations));
}

} // namespace office3ds::generated_adapter
