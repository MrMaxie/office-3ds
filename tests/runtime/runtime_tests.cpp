#include "office3ds/bridge/credential_claim.hpp"
#include "office3ds/bridge/pairing.hpp"
#include "office3ds/bridge/pairing_session.hpp"
#include "office3ds/core/application_flow.hpp"
#include "office3ds/core/avatar_policy.hpp"
#include "office3ds/core/credential_bundle.hpp"
#include "office3ds/core/dashboard_state.hpp"
#include "office3ds/core/pairing_entry.hpp"

#include <monocypher.h>

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <string>

namespace {

class FixedRandomSource final : public office3ds::bridge::RandomSource {
public:
  std::string token(std::size_t length) override {
    return std::string(length, calls_++ == 0 ? 'A' : 'B');
  }

private:
  int calls_ = 0;
};

office3ds::api::DashboardSnapshot dashboard() {
  office3ds::api::DashboardSnapshot snapshot;
  snapshot.profile = {"person-1", "Demo Person", "Developer", {}};
  snapshot.worklog = {{"2026-08-11", 420, 480}, {"2026-08-12", 480, 480}};
  snapshot.absences.push_back(
    {"absence-1", "Another Person", "2026-08-14", "2026-08-15", "2026-08-16", "Away", "Upcoming"});
  office3ds::api::ActivityEvent event;
  event.id = "event-1";
  event.recipient_id = "person-2";
  event.recipient_name = "Another Person";
  event.summary = "Project anniversary";
  event.allowed_recognition_values = {5, 3, 1};
  snapshot.activity.push_back(std::move(event));
  return snapshot;
}

void test_dashboard_state() {
  auto snapshot = dashboard();
  office3ds::core::DashboardNavigation navigation;
  navigation.reset_for_snapshot(snapshot);
  assert(navigation.subview() == office3ds::core::DashboardSubview::worklog);
  assert(navigation.selected_index() == 1);

  navigation.move_subview(2);
  assert(navigation.subview() == office3ds::core::DashboardSubview::activity);
  assert(navigation.selected_recognition_value(snapshot) == 5);
  navigation.move_recognition(1, snapshot);
  const auto recognition = navigation.begin_recognition(snapshot, "request-1");
  assert(recognition.has_value());
  assert(recognition->activity_id == "event-1");
  assert(recognition->recipient_id == "person-2");
  assert(recognition->value == 3);
  assert(recognition->message == "Project anniversary");
  assert(!navigation.begin_recognition(snapshot, "request-2").has_value());
  navigation.mark_recognition_failure();
  assert(navigation.recognition_status() == office3ds::core::RecognitionStatus::failure);

  office3ds::core::DashboardState state;
  state.set_snapshot(snapshot);
  state.mark_offline();
  assert(state.has_snapshot());
  assert(state.connection_state() == office3ds::core::ConnectionState::offline);
  assert(state.snapshot()->profile.display_name == "Demo Person");
}

void test_application_flow_and_pairing_entry() {
  office3ds::core::ApplicationFlow flow;
  flow.activate(false);
  assert(flow.view() == office3ds::core::ClientView::login);
  flow.start_loading(office3ds::core::LoadingContext::pairing);
  assert(flow.loading_progress().claiming_credential);
  assert(flow.loading_progress().total == 5);
  assert(!flow.set_loading_progress({0, 5, office3ds::api::DashboardLoadStage::profile, false}));
  assert(flow.set_loading_progress({1, 5, office3ds::api::DashboardLoadStage::profile, false}));
  assert(!flow.set_loading_progress({0, 5, office3ds::api::DashboardLoadStage::profile, false}));
  flow.show_dashboard();
  assert(flow.view() == office3ds::core::ClientView::dashboard);

  office3ds::core::PairingEntry entry;
  entry.open();
  entry.select_field(office3ds::core::PairingEntryField::ip);
  for (const char value : std::string("192.168.1.20")) {
    assert(entry.append(value));
  }
  entry.select_field(office3ds::core::PairingEntryField::port);
  for (const char value : std::string("38383")) {
    assert(entry.append(value));
  }
  entry.select_field(office3ds::core::PairingEntryField::code);
  for (const char value : std::string("12345678")) {
    assert(entry.append(value));
  }
  const auto manual = entry.submit();
  assert(manual.has_value() && !manual->uses_qr_secret);
  assert(manual->endpoint.host == "192.168.1.20");

  const office3ds::bridge::PairingEndpoint qr{"127.0.0.1", 38383,
                                              "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"};
  assert(entry.apply_qr_endpoint(qr));
  const auto scanned = entry.submit();
  assert(scanned.has_value() && scanned->uses_qr_secret);
  assert(scanned->pairing_material == qr.secret);

  assert(office3ds::core::maximum_avatar_response_size("profile", "profile") ==
         office3ds::core::kMaximumProfileAvatarResponseSize);
  assert(office3ds::core::maximum_avatar_response_size("profile", "list") ==
         office3ds::core::kMaximumListAvatarResponseSize);
  static_assert(office3ds::core::kMaximumConcurrentAvatarResponses == 2);
  static_assert(office3ds::core::kPreferredAvatarEdge == 32);
  static_assert(office3ds::core::kDegradedAvatarEdge == 16);
}

void test_credential_expiry() {
  const office3ds::core::CredentialBundle credential{"temporary-test-token", 2'000};
  const auto json = office3ds::core::serialize_credential_bundle(credential, 1'000);
  assert(json.has_value());
  assert(json->find("api_base_url") == std::string::npos);
  const auto parsed = office3ds::core::parse_credential_bundle(*json, 1'000);
  assert(parsed.has_value());
  assert(parsed->access_token == credential.access_token);
  assert(!office3ds::core::parse_credential_bundle(*json, 2'000).has_value());
  assert(!office3ds::core::serialize_credential_bundle({"bad token", 2'000}, 1'000).has_value());

  const office3ds::core::CredentialBundle skewed{"temporary-test-token", 2'000, -7'200};
  assert(office3ds::core::is_credential_usable(skewed, 9'199));
  assert(!office3ds::core::is_credential_usable(skewed, 9'200));
  const auto skewed_json = office3ds::core::serialize_credential_bundle(skewed, 9'000);
  assert(skewed_json.has_value());
  const auto parsed_skewed = office3ds::core::parse_credential_bundle(*skewed_json, 9'000);
  assert(parsed_skewed.has_value() && parsed_skewed->clock_offset_seconds == -7'200);
}

void test_pairing_session() {
  FixedRandomSource random;
  office3ds::bridge::PairingSession session(random);
  const auto now = std::chrono::system_clock::time_point{};
  const auto offer = session.create_offer("192.168.1.20", 38383, now, std::chrono::minutes(5));
  assert(offer.has_value());
  assert(offer->qr_payload.find("office-3ds://pair") == 0);
  assert(offer->pairing_code == "66666666");
  const auto endpoint = office3ds::bridge::decode_pairing_payload(offer->qr_payload);
  assert(endpoint.has_value());
  assert(endpoint->host == "192.168.1.20");
  assert(!session.consume_pairing_code("00000000", now + std::chrono::seconds(1)));
  assert(session.consume_pairing_code("66666666", now + std::chrono::seconds(2)));
  assert(!session.consume_pairing_code("66666666", now + std::chrono::seconds(3)));

  const auto bounded = session.create_offer("127.0.0.1", 38383, now, std::chrono::minutes(5));
  assert(bounded.has_value());
  for (int attempt = 0; attempt < 5; ++attempt) {
    assert(!session.consume_pairing_code("00000000", now + std::chrono::seconds(attempt)));
  }
  assert(!session.is_available(now + std::chrono::seconds(5)));

  const auto expired = session.create_offer("127.0.0.1", 38383, now, std::chrono::seconds(1));
  assert(expired.has_value());
  assert(!session.consume_secret(endpoint->secret, now + std::chrono::seconds(1)));
  assert(
    !office3ds::bridge::decode_pairing_payload("office-3ds://pair?host=192.0.2.1&port=38383&secret="
                                               "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA&version=2")
       .has_value());
}

void test_protected_claim() {
  std::array<std::uint8_t, 32> client_secret{};
  std::array<std::uint8_t, 32> client_public_key{};
  std::array<std::uint8_t, 32> server_secret{};
  std::array<std::uint8_t, 24> nonce{};
  for (std::size_t index = 0; index < client_secret.size(); ++index) {
    client_secret[index] = static_cast<std::uint8_t>(index + 1);
    server_secret[index] = static_cast<std::uint8_t>(index + 41);
  }
  for (std::size_t index = 0; index < nonce.size(); ++index) {
    nonce[index] = static_cast<std::uint8_t>(index + 81);
  }
  crypto_x25519_public_key(client_public_key.data(), client_secret.data());

  assert(!office3ds::bridge::serialize_credential_claim_request(
            {"12345678", false, std::array<std::uint8_t, 32>{}})
            .has_value());

  const auto request =
    office3ds::bridge::serialize_credential_claim_request({"12345678", false, client_public_key});
  assert(request.has_value());
  const auto parsed_request = office3ds::bridge::parse_credential_claim_request(*request);
  assert(parsed_request.has_value());
  assert(parsed_request->client_public_key == client_public_key);

  const office3ds::core::CredentialBundle credential{"temporary-test-token", 2'000};
  const auto claim = office3ds::bridge::encrypt_credential_claim(credential, client_public_key,
                                                                 server_secret, nonce, 1'000);
  assert(claim.has_value());
  const auto wire = office3ds::bridge::serialize_protected_credential_claim(*claim);
  assert(wire.has_value());
  assert(wire->find(credential.access_token) == std::string::npos);
  const auto parsed_claim = office3ds::bridge::parse_protected_credential_claim(*wire);
  assert(parsed_claim.has_value());
  const auto decrypted =
    office3ds::bridge::decrypt_credential_claim(*parsed_claim, client_secret, 8'200);
  assert(decrypted.has_value());
  assert(decrypted->access_token == credential.access_token);
  assert(decrypted->clock_offset_seconds == -7'200);

  auto tampered = *parsed_claim;
  tampered.ciphertext.front() ^= 1;
  assert(!office3ds::bridge::decrypt_credential_claim(tampered, client_secret, 1'000).has_value());
  assert(!office3ds::bridge::encrypt_credential_claim(credential, client_public_key, server_secret,
                                                      nonce, 2'000)
            .has_value());
}

} // namespace

int main() {
  test_dashboard_state();
  test_application_flow_and_pairing_entry();
  test_credential_expiry();
  test_pairing_session();
  test_protected_claim();
  return 0;
}
