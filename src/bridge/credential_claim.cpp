#include "office3ds/bridge/credential_claim.hpp"

#include "office3ds/bridge/pairing.hpp"

#include <monocypher.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <utility>

namespace office3ds::bridge {
namespace {

constexpr std::size_t kMaximumQrSecretLength = 128;
constexpr std::size_t kMaximumClaimCiphertextLength = 20U * 1024U;
constexpr std::size_t kMaximumClaimRequestJsonLength = 1024;
constexpr std::size_t kMaximumProtectedClaimJsonLength = 48U * 1024U;
constexpr int kClaimCredentialFormatVersion = 1;
constexpr std::string_view kClaimContext = "office-3ds-credential-claim-v2";
constexpr std::array<char, 16> kHexadecimal{'0', '1', '2', '3', '4', '5', '6', '7',
                                            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

template <std::size_t Size> std::string encode_hex(const std::array<std::uint8_t, Size> &bytes) {
  std::string result;
  result.reserve(Size * 2);
  for (const auto byte : bytes) {
    result.push_back(kHexadecimal[byte >> 4U]);
    result.push_back(kHexadecimal[byte & 0x0FU]);
  }
  return result;
}

std::string encode_hex(std::string_view bytes) {
  std::string result;
  result.reserve(bytes.size() * 2);
  for (const unsigned char byte : bytes) {
    result.push_back(kHexadecimal[byte >> 4U]);
    result.push_back(kHexadecimal[byte & 0x0FU]);
  }
  return result;
}

int hex_value(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

std::optional<std::string> decode_hex(std::string_view value, std::size_t maximum_size) {
  if (value.size() % 2 != 0 || value.size() / 2 > maximum_size) {
    return std::nullopt;
  }
  std::string bytes(value.size() / 2, '\0');
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    const auto high = hex_value(value[index * 2]);
    const auto low = hex_value(value[index * 2 + 1]);
    if (high < 0 || low < 0) {
      return std::nullopt;
    }
    bytes[index] = static_cast<char>((high << 4) | low);
  }
  return bytes;
}

template <std::size_t Size>
std::optional<std::array<std::uint8_t, Size>> decode_hex_array(std::string_view value) {
  const auto decoded = decode_hex(value, Size);
  if (!decoded.has_value() || decoded->size() != Size) {
    return std::nullopt;
  }
  std::array<std::uint8_t, Size> result{};
  std::copy(decoded->begin(), decoded->end(), result.begin());
  return result;
}

bool valid_qr_secret(std::string_view value) {
  return value.size() >= 32U && value.size() <= kMaximumQrSecretLength &&
         value.find_first_not_of("ABCDEFGHJKLMNPQRSTUVWXYZ23456789") == std::string_view::npos;
}

template <std::size_t Size> bool any_nonzero(const std::array<std::uint8_t, Size> &value) {
  return std::any_of(value.begin(), value.end(), [](std::uint8_t byte) { return byte != 0; });
}

std::optional<std::array<std::uint8_t, 32>>
derive_claim_key(const std::array<std::uint8_t, 32> &secret,
                 const std::array<std::uint8_t, 32> &peer_public_key) {
  std::array<std::uint8_t, 32> shared_secret{};
  std::array<std::uint8_t, 32> key{};
  crypto_x25519(shared_secret.data(), secret.data(), peer_public_key.data());
  if (!any_nonzero(shared_secret)) {
    crypto_wipe(shared_secret.data(), shared_secret.size());
    return std::nullopt;
  }
  crypto_blake2b(key.data(), key.size(), shared_secret.data(), shared_secret.size());
  crypto_wipe(shared_secret.data(), shared_secret.size());
  return key;
}

bool has_exact_fields(const nlohmann::json &document,
                      std::initializer_list<std::string_view> fields) {
  if (!document.is_object() || document.size() != fields.size()) {
    return false;
  }
  return std::all_of(fields.begin(), fields.end(),
                     [&](std::string_view field) { return document.contains(std::string(field)); });
}

std::optional<std::string> serialize_claim_credential(const core::CredentialBundle &credential,
                                                      std::int64_t server_epoch_seconds) {
  if (!core::is_credential_usable(credential, server_epoch_seconds)) {
    return std::nullopt;
  }
  return nlohmann::json{{"version", kClaimCredentialFormatVersion},
                        {"access_token", credential.access_token},
                        {"expires_at", credential.expires_at_epoch_seconds},
                        {"server_epoch", server_epoch_seconds}}
    .dump();
}

std::optional<core::CredentialBundle> parse_claim_credential(std::string_view plaintext,
                                                             std::int64_t device_epoch_seconds) {
  try {
    const auto document = nlohmann::json::parse(plaintext);
    if (!has_exact_fields(document, {"version", "access_token", "expires_at", "server_epoch"}) ||
        document.at("version").get<int>() != kClaimCredentialFormatVersion) {
      return std::nullopt;
    }
    const auto server_epoch = document.at("server_epoch").get<std::int64_t>();
    if (server_epoch <= 0 || device_epoch_seconds <= 0) {
      return std::nullopt;
    }
    core::CredentialBundle credential{document.at("access_token").get<std::string>(),
                                      document.at("expires_at").get<std::int64_t>(),
                                      server_epoch - device_epoch_seconds};
    return core::is_credential_usable(credential, device_epoch_seconds)
             ? std::optional<core::CredentialBundle>{std::move(credential)}
             : std::nullopt;
  } catch (const nlohmann::json::exception &) {
    return std::nullopt;
  }
}

} // namespace

std::optional<std::string>
serialize_credential_claim_request(const CredentialClaimRequest &request) {
  const bool material_is_valid = request.uses_qr_secret
                                   ? valid_qr_secret(request.pairing_material)
                                   : is_valid_pairing_code(request.pairing_material);
  if (!material_is_valid || !any_nonzero(request.client_public_key)) {
    return std::nullopt;
  }
  return nlohmann::json{{request.uses_qr_secret ? "secret" : "code", request.pairing_material},
                        {"client_public_key", encode_hex(request.client_public_key)}}
    .dump();
}

std::optional<CredentialClaimRequest> parse_credential_claim_request(std::string_view json) {
  if (json.empty() || json.size() > kMaximumClaimRequestJsonLength) {
    return std::nullopt;
  }
  try {
    const auto document = nlohmann::json::parse(json);
    const bool has_code = document.contains("code");
    const bool has_secret = document.contains("secret");
    if (has_code == has_secret ||
        !has_exact_fields(document, {has_secret ? "secret" : "code", "client_public_key"})) {
      return std::nullopt;
    }
    const auto material = document.at(has_secret ? "secret" : "code").get<std::string>();
    const auto public_key =
      decode_hex_array<32>(document.at("client_public_key").get<std::string>());
    const bool material_is_valid =
      has_secret ? valid_qr_secret(material) : is_valid_pairing_code(material);
    return material_is_valid && public_key.has_value() && any_nonzero(*public_key)
             ? std::optional<CredentialClaimRequest>{{material, has_secret, *public_key}}
             : std::nullopt;
  } catch (const nlohmann::json::exception &) {
    return std::nullopt;
  }
}

std::optional<std::string>
serialize_protected_credential_claim(const ProtectedCredentialClaim &claim) {
  if (claim.ciphertext.empty() || claim.ciphertext.size() > kMaximumClaimCiphertextLength) {
    return std::nullopt;
  }
  return nlohmann::json{{"server_public_key", encode_hex(claim.server_public_key)},
                        {"nonce", encode_hex(claim.nonce)},
                        {"authentication_tag", encode_hex(claim.authentication_tag)},
                        {"ciphertext", encode_hex(claim.ciphertext)}}
    .dump();
}

std::optional<ProtectedCredentialClaim> parse_protected_credential_claim(std::string_view json) {
  if (json.empty() || json.size() > kMaximumProtectedClaimJsonLength) {
    return std::nullopt;
  }
  try {
    const auto document = nlohmann::json::parse(json);
    if (!has_exact_fields(document,
                          {"server_public_key", "nonce", "authentication_tag", "ciphertext"})) {
      return std::nullopt;
    }
    const auto public_key =
      decode_hex_array<32>(document.at("server_public_key").get<std::string>());
    const auto nonce = decode_hex_array<24>(document.at("nonce").get<std::string>());
    const auto tag = decode_hex_array<16>(document.at("authentication_tag").get<std::string>());
    const auto ciphertext =
      decode_hex(document.at("ciphertext").get<std::string>(), kMaximumClaimCiphertextLength);
    if (!public_key.has_value() || !any_nonzero(*public_key) || !nonce.has_value() ||
        !tag.has_value() || !ciphertext.has_value() || ciphertext->empty()) {
      return std::nullopt;
    }
    return ProtectedCredentialClaim{*public_key, *nonce, *tag, std::move(*ciphertext)};
  } catch (const nlohmann::json::exception &) {
    return std::nullopt;
  }
}

std::optional<ProtectedCredentialClaim> encrypt_credential_claim(
  const core::CredentialBundle &credential, const std::array<std::uint8_t, 32> &client_public_key,
  const std::array<std::uint8_t, 32> &server_secret, const std::array<std::uint8_t, 24> &nonce,
  std::int64_t now_epoch_seconds) {
  auto plaintext = serialize_claim_credential(credential, now_epoch_seconds);
  if (!plaintext.has_value()) {
    return std::nullopt;
  }
  ProtectedCredentialClaim claim;
  crypto_x25519_public_key(claim.server_public_key.data(), server_secret.data());
  claim.nonce = nonce;
  claim.ciphertext.resize(plaintext->size());
  auto key = derive_claim_key(server_secret, client_public_key);
  if (!key.has_value()) {
    crypto_wipe(plaintext->data(), plaintext->size());
    return std::nullopt;
  }
  crypto_aead_lock(reinterpret_cast<std::uint8_t *>(claim.ciphertext.data()),
                   claim.authentication_tag.data(), key->data(), claim.nonce.data(),
                   reinterpret_cast<const std::uint8_t *>(kClaimContext.data()),
                   kClaimContext.size(), reinterpret_cast<const std::uint8_t *>(plaintext->data()),
                   plaintext->size());
  crypto_wipe(key->data(), key->size());
  crypto_wipe(plaintext->data(), plaintext->size());
  return claim;
}

std::optional<core::CredentialBundle>
decrypt_credential_claim(const ProtectedCredentialClaim &claim,
                         const std::array<std::uint8_t, 32> &client_secret,
                         std::int64_t now_epoch_seconds) {
  if (claim.ciphertext.empty() || claim.ciphertext.size() > kMaximumClaimCiphertextLength) {
    return std::nullopt;
  }
  auto key = derive_claim_key(client_secret, claim.server_public_key);
  if (!key.has_value()) {
    return std::nullopt;
  }
  std::string plaintext(claim.ciphertext.size(), '\0');
  const auto status = crypto_aead_unlock(
    reinterpret_cast<std::uint8_t *>(plaintext.data()), claim.authentication_tag.data(),
    key->data(), claim.nonce.data(), reinterpret_cast<const std::uint8_t *>(kClaimContext.data()),
    kClaimContext.size(), reinterpret_cast<const std::uint8_t *>(claim.ciphertext.data()),
    claim.ciphertext.size());
  crypto_wipe(key->data(), key->size());
  if (status != 0) {
    crypto_wipe(plaintext.data(), plaintext.size());
    return std::nullopt;
  }
  auto credential = parse_claim_credential(plaintext, now_epoch_seconds);
  crypto_wipe(plaintext.data(), plaintext.size());
  return credential;
}

} // namespace office3ds::bridge
