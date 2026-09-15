#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace office3ds::api {

struct Profile {
  std::string id;
  std::string display_name;
  std::string role;
  std::string avatar_url;
};

struct WorklogDay {
  std::string date;
  std::int32_t minutes = 0;
  std::int32_t expected_minutes = 0;
};

struct Absence {
  std::string id;
  std::string person_name;
  std::string starts_on;
  std::string ends_on;
  std::string return_date;
  std::string label;
  std::string group;
  std::string avatar_url;
};

struct ActivityEvent {
  std::string id;
  std::string recipient_id;
  std::string recipient_name;
  std::string occurred_at;
  std::string age_label;
  std::string summary;
  std::vector<std::int32_t> allowed_recognition_values;
  std::string avatar_url;
};

struct DashboardSnapshot {
  Profile profile;
  std::vector<WorklogDay> worklog;
  std::vector<Absence> absences;
  std::vector<ActivityEvent> activity;
};

enum class DashboardLoadStage : std::uint8_t {
  profile,
  worklog,
  absences,
  activity,
  complete,
};

enum class AdapterResult : std::uint8_t {
  ok,
  unauthorized,
  unavailable,
  invalid_response,
};

struct DashboardLoadUpdate {
  DashboardLoadStage stage = DashboardLoadStage::profile;
  std::uint8_t completed = 0;
  std::uint8_t total = 4;
  bool finished = false;
  AdapterResult result = AdapterResult::ok;
  std::optional<DashboardSnapshot> snapshot;
};

struct RecognitionRequest {
  std::string activity_id;
  std::string recipient_id;
  std::int32_t value = 0;
  std::string message;
  std::string request_id;
};

struct RecognitionResult {
  AdapterResult result = AdapterResult::ok;
  bool accepted = false;
  std::string recognition_id;
  std::string message;
};

} // namespace office3ds::api
