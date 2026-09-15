#pragma once

#include "office3ds/api/models.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace office3ds::generated_adapter {

class DemoJsonCodec {
public:
  [[nodiscard]] static std::optional<api::Profile> decode_profile(std::string_view json);
  [[nodiscard]] static std::optional<std::vector<api::WorklogDay>>
  decode_worklog(std::string_view json);
  [[nodiscard]] static std::optional<std::vector<api::Absence>>
  decode_absences(std::string_view json, const api::Profile &profile);
  [[nodiscard]] static std::optional<std::vector<api::ActivityEvent>>
  decode_activity(std::string_view json, const api::Profile &profile);

  [[nodiscard]] static std::optional<std::string>
  encode_recognition(const api::RecognitionRequest &request);
  [[nodiscard]] static std::optional<api::RecognitionResult>
  decode_recognition(std::string_view json, std::uint16_t status);
};

} // namespace office3ds::generated_adapter
