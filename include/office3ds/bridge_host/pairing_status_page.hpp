#pragma once

#include "office3ds/bridge/pairing_session.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace office3ds::bridge_host {

[[nodiscard]] std::string
render_pairing_status_page(std::string_view product_display_name, std::string_view advertised_host,
                           std::uint16_t port, std::string_view accent_color,
                           const bridge::PairingOffer &offer, bool available,
                           bool credential_usable, bool claimed, std::chrono::seconds remaining,
                           std::string_view reset_token, std::string_view claim_status);

} // namespace office3ds::bridge_host
