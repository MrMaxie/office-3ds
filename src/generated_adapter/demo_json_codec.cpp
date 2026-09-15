#include "office3ds/generated_adapter/demo_json_codec.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace office3ds::generated_adapter {
namespace {

using Json = nlohmann::json;

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

std::optional<std::string> required_string(const Json &object, const char *key) {
  const auto found = object.find(key);
  if (found == object.end() || !found->is_string()) {
    return std::nullopt;
  }
  auto value = found->get<std::string>();
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
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

std::string optional_string(const Json &object, const char *key) {
  const auto found = object.find(key);
  return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

const Json *items_array(const Json &document) {
  if (!document.is_object()) {
    return nullptr;
  }
  const auto found = document.find("items");
  return found != document.end() && found->is_array() ? &*found : nullptr;
}

bool recognition_value(std::int32_t value) { return value == 5 || value == 3 || value == 1; }

bool valid_identifier(std::string_view value) {
  if (value.empty() || value.size() > 64) {
    return false;
  }
  for (const unsigned char character : value) {
    if ((character < 'a' || character > 'z') && (character < 'A' || character > 'Z') &&
        (character < '0' || character > '9') && character != '-' && character != '_') {
      return false;
    }
  }
  return true;
}

} // namespace

std::optional<api::Profile> DemoJsonCodec::decode_profile(std::string_view json) {
  const auto document = parse_json(json);
  if (!document || !document->is_object()) {
    return std::nullopt;
  }
  const auto id = required_string(*document, "id");
  const auto display_name = required_string(*document, "display_name");
  if (!id || !display_name) {
    return std::nullopt;
  }
  return api::Profile{*id, *display_name, optional_string(*document, "role"),
                      optional_string(*document, "avatar_url")};
}

std::optional<std::vector<api::WorklogDay>> DemoJsonCodec::decode_worklog(std::string_view json) {
  const auto document = parse_json(json);
  const auto *items = document ? items_array(*document) : nullptr;
  if (items == nullptr) {
    return std::nullopt;
  }
  std::vector<api::WorklogDay> result;
  result.reserve(items->size());
  for (const auto &item : *items) {
    if (!item.is_object()) {
      return std::nullopt;
    }
    const auto date = required_string(item, "date");
    const auto minutes = required_int32(item, "minutes");
    if (!date || !minutes || *minutes < 0 || *minutes > 1440) {
      return std::nullopt;
    }
    const auto expected = required_int32(item, "expected_minutes").value_or(0);
    if (expected < 0 || expected > 1440) {
      return std::nullopt;
    }
    result.push_back(api::WorklogDay{*date, *minutes, expected});
  }
  return result;
}

std::optional<std::vector<api::Absence>>
DemoJsonCodec::decode_absences(std::string_view json, const api::Profile &profile) {
  (void)profile;
  const auto document = parse_json(json);
  const auto *items = document ? items_array(*document) : nullptr;
  if (items == nullptr || profile.id.empty() || profile.display_name.empty()) {
    return std::nullopt;
  }
  std::vector<api::Absence> result;
  result.reserve(items->size());
  for (const auto &item : *items) {
    if (!item.is_object()) {
      return std::nullopt;
    }
    const auto id = required_string(item, "id");
    const auto person_name = required_string(item, "person_name");
    const auto starts_on = required_string(item, "starts_on");
    const auto ends_on = required_string(item, "ends_on");
    const auto label = required_string(item, "label");
    if (!id || !person_name || !starts_on || !ends_on || !label) {
      return std::nullopt;
    }
    result.push_back(
      api::Absence{*id, *person_name, *starts_on, *ends_on, optional_string(item, "return_date"),
                   *label, optional_string(item, "group"), optional_string(item, "avatar_url")});
  }
  return result;
}

std::optional<std::vector<api::ActivityEvent>>
DemoJsonCodec::decode_activity(std::string_view json, const api::Profile &profile) {
  (void)profile;
  const auto document = parse_json(json);
  const auto *items = document ? items_array(*document) : nullptr;
  if (items == nullptr || profile.id.empty() || profile.display_name.empty()) {
    return std::nullopt;
  }
  std::vector<api::ActivityEvent> result;
  result.reserve(items->size());
  for (const auto &item : *items) {
    if (!item.is_object()) {
      return std::nullopt;
    }
    const auto id_number = required_int32(item, "id");
    const auto id_string = required_string(item, "id");
    const auto id = id_number && *id_number > 0
                      ? std::optional<std::string>(std::to_string(*id_number))
                      : id_string;
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
      const auto value = entry.get<std::int32_t>();
      if (!recognition_value(value)) {
        return std::nullopt;
      }
      allowed_values.push_back(value);
    }
    if (allowed_values.empty()) {
      return std::nullopt;
    }
    result.push_back(api::ActivityEvent{
      *id, *recipient_id, *recipient_name, *occurred_at, optional_string(item, "age_label"),
      *summary, std::move(allowed_values), optional_string(item, "avatar_url")});
  }
  return result;
}

std::optional<std::string>
DemoJsonCodec::encode_recognition(const api::RecognitionRequest &request) {
  if (!recognition_value(request.value) || !valid_identifier(request.request_id) ||
      !valid_identifier(request.recipient_id) || request.activity_id.empty() ||
      request.activity_id.size() > 20 || request.message.size() > 280) {
    return std::nullopt;
  }
  std::int64_t source_event_id = 0;
  try {
    std::size_t consumed = 0;
    source_event_id = std::stoll(request.activity_id, &consumed);
    if (consumed != request.activity_id.size() || source_event_id <= 0) {
      return std::nullopt;
    }
  } catch (const std::exception &) {
    return std::nullopt;
  }
  return Json{{"request_id", request.request_id},
              {"source_event_id", source_event_id},
              {"recipient_id", request.recipient_id},
              {"value", request.value},
              {"comment", request.message}}
    .dump();
}

std::optional<api::RecognitionResult> DemoJsonCodec::decode_recognition(std::string_view json,
                                                                        std::uint16_t status) {
  if (status != 200 && status != 201) {
    return std::nullopt;
  }
  const auto document = parse_json(json);
  if (!document || !document->is_object()) {
    return std::nullopt;
  }
  const auto id = document->find("id");
  if (id == document->end() || (!id->is_number_integer() && !id->is_string())) {
    return std::nullopt;
  }
  const auto recognition_id =
    id->is_string() ? id->get<std::string>() : std::to_string(id->get<std::int64_t>());
  if (recognition_id.empty()) {
    return std::nullopt;
  }
  return api::RecognitionResult{api::AdapterResult::ok, true, recognition_id,
                                optional_string(*document, "message")};
}

} // namespace office3ds::generated_adapter
