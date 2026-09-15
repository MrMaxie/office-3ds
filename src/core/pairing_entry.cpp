#include "office3ds/core/pairing_entry.hpp"

#include <cctype>

namespace office3ds::core {

void PairingEntry::open() {
  open_ = true;
  field_ = PairingEntryField::code;
  code_text_.clear();
  ip_text_.clear();
  port_text_.clear();
  qr_secret_.clear();
}

void PairingEntry::cancel() {
  open_ = false;
  code_text_.clear();
  ip_text_.clear();
  port_text_.clear();
  qr_secret_.clear();
}

bool PairingEntry::is_open() const noexcept { return open_; }

PairingEntryField PairingEntry::field() const noexcept { return field_; }

std::string_view PairingEntry::code_text() const noexcept { return code_text_; }

std::string_view PairingEntry::ip_text() const noexcept { return ip_text_; }

std::string_view PairingEntry::port_text() const noexcept { return port_text_; }

bool PairingEntry::selected_value_empty() const noexcept {
  switch (field_) {
  case PairingEntryField::code:
    return code_text_.empty();
  case PairingEntryField::ip:
    return ip_text_.empty();
  case PairingEntryField::port:
    return port_text_.empty();
  }
  return true;
}

void PairingEntry::select_field(PairingEntryField field) noexcept {
  if (open_) {
    field_ = field;
  }
}

void PairingEntry::move_field(int offset) noexcept {
  if (!open_ || offset == 0) {
    return;
  }
  constexpr int kFieldCount = 3;
  auto index = static_cast<int>(field_);
  index = (index + offset % kFieldCount + kFieldCount) % kFieldCount;
  field_ = static_cast<PairingEntryField>(index);
}

bool PairingEntry::apply_qr_endpoint(const bridge::PairingEndpoint &endpoint) {
  if (!open_ || !bridge::is_local_bridge_ipv4(endpoint.host) || endpoint.port == 0 ||
      endpoint.secret.empty()) {
    return false;
  }
  code_text_.clear();
  ip_text_ = endpoint.host;
  port_text_ = std::to_string(endpoint.port);
  qr_secret_ = endpoint.secret;
  field_ = PairingEntryField::code;
  return true;
}

bool PairingEntry::append(char value) {
  if (!open_) {
    return false;
  }
  const auto is_digit = std::isdigit(static_cast<unsigned char>(value)) != 0;
  if (field_ == PairingEntryField::code) {
    if (code_text_.size() >= kPairingCodeLength || !is_digit) {
      return false;
    }
    code_text_ += value;
    qr_secret_.clear();
    return true;
  }
  if (field_ == PairingEntryField::ip) {
    if (ip_text_.size() >= kMaximumIpLength || !(is_digit || value == '.')) {
      return false;
    }
    ip_text_ += value;
    return true;
  }
  if (port_text_.size() >= kMaximumPortLength || !is_digit) {
    return false;
  }
  port_text_ += value;
  return true;
}

void PairingEntry::erase() noexcept {
  if (!open_) {
    return;
  }
  qr_secret_.clear();
  auto *value = &code_text_;
  if (field_ == PairingEntryField::ip) {
    value = &ip_text_;
  } else if (field_ == PairingEntryField::port) {
    value = &port_text_;
  }
  if (!value->empty()) {
    value->pop_back();
  }
}

std::optional<PairingInput> PairingEntry::submit() const {
  if (!open_ || (qr_secret_.empty() && !bridge::is_valid_pairing_code(code_text_))) {
    return std::nullopt;
  }
  bridge::PairingEndpoint endpoint;
  if (!bridge::parse_pairing_endpoint(ip_text_ + ':' + port_text_, endpoint)) {
    return std::nullopt;
  }
  return PairingInput{std::move(endpoint), qr_secret_.empty() ? code_text_ : qr_secret_,
                      !qr_secret_.empty()};
}

} // namespace office3ds::core
