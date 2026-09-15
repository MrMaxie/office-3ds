#pragma once

#include "office3ds/bridge/pairing.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace office3ds::core {

enum class PairingEntryField {
  code,
  ip,
  port,
};

struct PairingInput {
  bridge::PairingEndpoint endpoint;
  std::string pairing_material;
  bool uses_qr_secret = false;
};

class PairingEntry {
public:
  void open();
  void cancel();
  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] PairingEntryField field() const noexcept;
  [[nodiscard]] std::string_view code_text() const noexcept;
  [[nodiscard]] std::string_view ip_text() const noexcept;
  [[nodiscard]] std::string_view port_text() const noexcept;
  [[nodiscard]] bool selected_value_empty() const noexcept;

  void select_field(PairingEntryField field) noexcept;
  void move_field(int offset) noexcept;
  [[nodiscard]] bool apply_qr_endpoint(const bridge::PairingEndpoint &endpoint);
  [[nodiscard]] bool append(char value);
  void erase() noexcept;
  [[nodiscard]] std::optional<PairingInput> submit() const;

private:
  static constexpr std::size_t kPairingCodeLength = 8U;
  static constexpr std::size_t kMaximumIpLength = 15U;
  static constexpr std::size_t kMaximumPortLength = 5U;

  bool open_ = false;
  PairingEntryField field_ = PairingEntryField::code;
  std::string code_text_;
  std::string ip_text_;
  std::string port_text_;
  std::string qr_secret_;
};

} // namespace office3ds::core
