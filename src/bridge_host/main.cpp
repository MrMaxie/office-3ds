#include "office3ds/bridge_host/credential_source.hpp"
#include "office3ds/bridge_host/server.hpp"

#include "product_config.hpp"

#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct Options {
  std::string bind_address{"127.0.0.1"};
  std::string advertised_host{"127.0.0.1"};
  std::uint16_t port = 0;
  std::int64_t lifetime_seconds = 300;
};

template <typename Integer> Integer parse_integer(std::string_view value, std::string_view option) {
  Integer result{};
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
    throw std::invalid_argument("Invalid numeric value for " + std::string(option));
  }
  return result;
}

Options parse_options(int argc, char **argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view option(argv[index]);
    if (index + 1 >= argc) {
      throw std::invalid_argument("Missing value for " + std::string(option));
    }
    const std::string_view value(argv[++index]);
    if (option == "--bind") {
      options.bind_address = value;
    } else if (option == "--advertise") {
      options.advertised_host = value;
    } else if (option == "--port") {
      const auto port = parse_integer<unsigned>(value, option);
      if (port > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument("Port must be between 0 and 65535");
      }
      options.port = static_cast<std::uint16_t>(port);
    } else if (option == "--lifetime-seconds") {
      options.lifetime_seconds = parse_integer<std::int64_t>(value, option);
      if (options.lifetime_seconds <= 0 || options.lifetime_seconds > 15 * 60) {
        throw std::invalid_argument("Pairing lifetime must be between 1 and 900 seconds");
      }
    } else {
      throw std::invalid_argument("Unknown option: " + std::string(option));
    }
  }
  return options;
}

std::int64_t now_epoch_seconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
           std::chrono::system_clock::now().time_since_epoch())
    .count();
}

void wipe(std::string &value) noexcept {
  volatile char *data = value.empty() ? nullptr : value.data();
  for (std::size_t index = 0; data != nullptr && index < value.size(); ++index) {
    data[index] = '\0';
  }
  value.clear();
}

} // namespace

int main(int argc, char **argv) {
  try {
    const auto options = parse_options(argc, argv);
    const auto &descriptor = office3ds::generated::product_descriptor();
    const auto *credential_path = std::getenv(descriptor.token_file_environment.c_str());
    if (credential_path == nullptr || *credential_path == '\0') {
      throw std::runtime_error("The generated token-file environment variable is not configured");
    }
    auto credential = office3ds::bridge_host::load_credential_file(
      std::filesystem::path(credential_path), descriptor.credential_expiry_policy,
      now_epoch_seconds());
    if (!credential.has_value()) {
      throw std::runtime_error("The configured credential file is invalid or expired");
    }

    office3ds::bridge_host::CredentialClaimServer server(
      {std::move(*credential), options.bind_address, options.port, descriptor.display_name,
       office3ds::generated::product_presentation().palette.count("accent") != 0U
         ? office3ds::generated::product_presentation().palette.at("accent")
         : "#2A6F97"});
    wipe(credential->access_token);
    auto offer =
      server.begin_pairing(options.advertised_host, std::chrono::seconds(options.lifetime_seconds));
    if (!offer.has_value()) {
      throw std::runtime_error("The bridge could not create a local pairing offer");
    }

    const auto page_url =
      "http://" + options.advertised_host + ':' + std::to_string(server.bind()) + '/';
    std::cout << "\n+----------------------------------------------------------+\n"
              << "| " << descriptor.display_name << " pairing bridge\n"
              << "+----------------------------------------------------------+\n"
              << "  Status page : " << page_url << '\n'
              << "  Pairing code: " << offer->pairing_code << '\n'
              << "  Valid once for " << options.lifetime_seconds << " seconds\n"
              << "+----------------------------------------------------------+\n\n";
    wipe(offer->qr_payload);
    wipe(offer->pairing_code);
    if (!server.listen()) {
      throw std::runtime_error("The bridge could not listen on the configured address");
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 1;
  }
}
