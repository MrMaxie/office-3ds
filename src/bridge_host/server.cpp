#include "office3ds/bridge_host/server.hpp"

#include "office3ds/bridge/credential_claim.hpp"
#include "office3ds/bridge_host/pairing_status_page.hpp"

#include <httplib.h>
#include <monocypher.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <bcrypt.h>
#else
#include <cerrno>
#include <sys/random.h>
#endif

namespace office3ds::bridge_host {
namespace {

constexpr std::size_t kMaximumClaimRequestBytes = 1024U;

std::int64_t epoch_seconds(std::chrono::system_clock::time_point now) {
  return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

template <std::size_t Size> bool secure_random(std::array<std::uint8_t, Size> &bytes) {
#ifdef _WIN32
  return BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                         BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto received = ::getrandom(bytes.data() + offset, bytes.size() - offset, 0);
    if (received < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    offset += static_cast<std::size_t>(received);
  }
  return true;
#endif
}

void secure_wipe(std::string &value) noexcept {
  if (!value.empty()) {
    crypto_wipe(value.data(), value.size());
  }
  value.clear();
}

void error_response(httplib::Response &response, int status, std::string_view code) {
  response.status = status;
  response.set_content("{\"error\":\"" + std::string(code) + "\"}", "application/json");
  response.set_header("Cache-Control", "no-store");
  response.set_header("X-Content-Type-Options", "nosniff");
}

void private_response_headers(httplib::Response &response) {
  response.set_header("Cache-Control", "no-store, max-age=0");
  response.set_header("Pragma", "no-cache");
  response.set_header("Referrer-Policy", "no-referrer");
  response.set_header("X-Content-Type-Options", "nosniff");
}

bool constant_time_equal(std::string_view left, std::string_view right) {
  const auto size = std::max(left.size(), right.size());
  unsigned char difference = static_cast<unsigned char>(left.size() ^ right.size());
  for (std::size_t index = 0; index < size; ++index) {
    const auto left_value = index < left.size() ? static_cast<unsigned char>(left[index]) : 0U;
    const auto right_value = index < right.size() ? static_cast<unsigned char>(right[index]) : 0U;
    difference |= static_cast<unsigned char>(left_value ^ right_value);
  }
  return difference == 0U;
}

} // namespace

class CredentialClaimServer::Impl {
public:
  explicit Impl(ClaimServerConfig config) : config_(std::move(config)), session_(random_source_) {
    if (!core::is_credential_usable(config_.credential,
                                    epoch_seconds(std::chrono::system_clock::now()))) {
      throw std::invalid_argument("Credential claim server requires an unexpired credential");
    }
    server_.set_payload_max_length(kMaximumClaimRequestBytes);
    server_.set_read_timeout(5, 0);
    server_.set_write_timeout(5, 0);
    server_.Post("/v2/claim", [this](const httplib::Request &request, httplib::Response &response) {
      claim(request, response);
    });
    server_.Post("/reset", [this](const httplib::Request &request, httplib::Response &response) {
      reset_pairing(request, response);
    });
    server_.Get("/", [this](const httplib::Request &, httplib::Response &response) {
      std::scoped_lock lock(mutex_);
      private_response_headers(response);
      if (!offer_.has_value()) {
        response.status = 503;
        response.set_content("Pairing has not started.", "text/plain; charset=utf-8");
        return;
      }
      const auto now = std::chrono::system_clock::now();
      const auto credential_usable =
        core::is_credential_usable(config_.credential, epoch_seconds(now));
      auto available = session_.is_available(now) && credential_usable;
      if (!available && !offer_claimed_ && credential_usable && now >= offer_->expires_at) {
        available = renew_pairing_locked(now);
      }
      const auto remaining =
        available ? std::chrono::duration_cast<std::chrono::seconds>(offer_->expires_at - now)
                  : std::chrono::seconds::zero();
      response.set_content(
        render_pairing_status_page(config_.product_display_name, advertised_host_, bound_port_,
                                   config_.accent_color, *offer_, available, credential_usable,
                                   offer_claimed_, remaining, reset_token_, claim_status_),
        "text/html; charset=utf-8");
    });
    server_.Get("/health", [this](const httplib::Request &, httplib::Response &response) {
      std::scoped_lock lock(mutex_);
      const auto available = session_.is_available(std::chrono::system_clock::now()) &&
                             core::is_credential_usable(
                               config_.credential, epoch_seconds(std::chrono::system_clock::now()));
      response.status = 200;
      response.set_content("{\"status\":\"" + std::string(available ? "ready" : "unavailable") +
                             "\",\"last_claim\":\"" + claim_status_ + "\"}",
                           "application/json");
      response.set_header("Cache-Control", "no-store");
      response.set_header("X-Content-Type-Options", "nosniff");
    });
    server_.set_error_handler([](const httplib::Request &, httplib::Response &response) {
      if (!response.body.empty()) {
        response.set_header("Cache-Control", "no-store");
        response.set_header("X-Content-Type-Options", "nosniff");
        return;
      }
      error_response(response, response.status == 413 ? 413 : 404,
                     response.status == 413 ? "request_too_large" : "not_found");
    });
  }

  ~Impl() {
    stop();
    session_.invalidate();
    secure_wipe(config_.credential.access_token);
  }

  std::uint16_t bind() {
    if (bound_port_ != 0U) {
      return bound_port_;
    }
    const auto port = config_.port == 0U ? server_.bind_to_any_port(config_.bind_address)
                                         : (server_.bind_to_port(config_.bind_address, config_.port)
                                              ? static_cast<int>(config_.port)
                                              : -1);
    if (port <= 0 || port > std::numeric_limits<std::uint16_t>::max()) {
      return 0;
    }
    bound_port_ = static_cast<std::uint16_t>(port);
    return bound_port_;
  }

  std::optional<bridge::PairingOffer> begin_pairing(std::string advertised_host,
                                                    std::chrono::seconds lifetime,
                                                    std::chrono::system_clock::time_point now) {
    if (bind() == 0U) {
      return std::nullopt;
    }
    std::scoped_lock lock(mutex_);
    if (!core::is_credential_usable(config_.credential, epoch_seconds(now))) {
      return std::nullopt;
    }
    advertised_host_ = std::move(advertised_host);
    pairing_lifetime_ = lifetime;
    if (!renew_pairing_locked(now)) {
      return std::nullopt;
    }
    return offer_;
  }

  bool listen() {
    if (bind() == 0U) {
      return false;
    }
    return server_.listen_after_bind();
  }

  void waitUntilReady() const { server_.wait_until_ready(); }

  void stop() { server_.stop(); }

private:
  bool renew_pairing_locked(std::chrono::system_clock::time_point now) {
    if (advertised_host_.empty() || pairing_lifetime_ <= std::chrono::seconds::zero() ||
        !core::is_credential_usable(config_.credential, epoch_seconds(now))) {
      return false;
    }
    auto next_offer = session_.create_offer(advertised_host_, bound_port_, now, pairing_lifetime_);
    auto next_reset_token = random_source_.token(32);
    if (!next_offer.has_value() || next_reset_token.size() != 32U) {
      return false;
    }
    offer_ = std::move(next_offer);
    reset_token_ = std::move(next_reset_token);
    offer_claimed_ = false;
    claim_status_ = "Ready for one device";
    return true;
  }

  void reset_pairing(const httplib::Request &request, httplib::Response &response) {
    std::scoped_lock lock(mutex_);
    private_response_headers(response);
    if (!request.has_param("csrf_token") ||
        !constant_time_equal(request.get_param_value("csrf_token"), reset_token_)) {
      error_response(response, 403, "reset_rejected");
      return;
    }
    if (!renew_pairing_locked(std::chrono::system_clock::now())) {
      error_response(response, 410, "credential_expired");
      return;
    }
    response.status = 303;
    response.set_header("Location", "/");
  }

  void claim(const httplib::Request &request, httplib::Response &response) {
    const auto claim_request = bridge::parse_credential_claim_request(request.body);
    if (!claim_request.has_value()) {
      std::scoped_lock lock(mutex_);
      claim_status_ = "Last attempt had an invalid request";
      error_response(response, 400, "invalid_claim");
      return;
    }

    std::scoped_lock lock(mutex_);
    const auto now = std::chrono::system_clock::now();
    if (!session_.is_available(now)) {
      claim_status_ = "Pairing session is no longer available";
      error_response(response, 401, "pairing_rejected");
      return;
    }
    if (!core::is_credential_usable(config_.credential, epoch_seconds(now))) {
      session_.invalidate();
      claim_status_ = "Credential expired before pairing";
      error_response(response, 410, "credential_expired");
      return;
    }
    const auto accepted = claim_request->uses_qr_secret
                            ? session_.consume_secret(claim_request->pairing_material, now)
                            : session_.consume_pairing_code(claim_request->pairing_material, now);
    if (!accepted) {
      claim_status_ = "Last pairing code or QR secret was rejected";
      error_response(response, 401, "pairing_rejected");
      return;
    }

    std::array<std::uint8_t, 32> server_secret{};
    std::array<std::uint8_t, 24> nonce{};
    if (!secure_random(server_secret) || !secure_random(nonce)) {
      crypto_wipe(server_secret.data(), server_secret.size());
      crypto_wipe(nonce.data(), nonce.size());
      claim_status_ = "Claim protection could not start";
      error_response(response, 500, "random_source_unavailable");
      return;
    }
    const auto protected_claim =
      bridge::encrypt_credential_claim(config_.credential, claim_request->client_public_key,
                                       server_secret, nonce, epoch_seconds(now));
    crypto_wipe(server_secret.data(), server_secret.size());
    crypto_wipe(nonce.data(), nonce.size());
    if (!protected_claim.has_value()) {
      claim_status_ = "Claim protection failed";
      error_response(response, 500, "claim_protection_failed");
      return;
    }
    const auto body = bridge::serialize_protected_credential_claim(*protected_claim);
    if (!body.has_value()) {
      claim_status_ = "Claim serialization failed";
      error_response(response, 500, "claim_serialization_failed");
      return;
    }
    response.status = 200;
    offer_claimed_ = true;
    claim_status_ = "Pairing completed";
    response.set_content(*body, "application/json");
    response.set_header("Cache-Control", "no-store");
    response.set_header("X-Content-Type-Options", "nosniff");
  }

  ClaimServerConfig config_;
  bridge::SystemRandomSource random_source_;
  bridge::PairingSession session_;
  httplib::Server server_;
  mutable std::mutex mutex_;
  std::uint16_t bound_port_ = 0;
  std::string advertised_host_;
  std::optional<bridge::PairingOffer> offer_;
  std::chrono::seconds pairing_lifetime_{0};
  std::string reset_token_;
  bool offer_claimed_ = false;
  std::string claim_status_{"Pairing has not started"};
};

CredentialClaimServer::CredentialClaimServer(ClaimServerConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

CredentialClaimServer::~CredentialClaimServer() = default;

std::uint16_t CredentialClaimServer::bind() { return impl_->bind(); }

std::optional<bridge::PairingOffer>
CredentialClaimServer::begin_pairing(std::string advertised_host, std::chrono::seconds lifetime,
                                     std::chrono::system_clock::time_point now) {
  return impl_->begin_pairing(std::move(advertised_host), lifetime, now);
}

bool CredentialClaimServer::listen() { return impl_->listen(); }

void CredentialClaimServer::waitUntilReady() const { impl_->waitUntilReady(); }

void CredentialClaimServer::stop() { impl_->stop(); }

} // namespace office3ds::bridge_host
