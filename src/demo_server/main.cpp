#include <office3ds/demo/demo_service.hpp>
#include <office3ds/demo/server.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct Options {
  std::string command{"serve"};
  std::string database{"office-3ds-demo.sqlite3"};
  std::string bind{"127.0.0.1"};
  std::string output;
  std::uint16_t port{8080};
  std::int64_t ttl_seconds{3600};
};

template <typename Integer> Integer parseInteger(std::string_view value, std::string_view option) {
  Integer result{};
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
    throw std::invalid_argument("Invalid numeric value for " + std::string(option));
  }
  return result;
}

Options parseOptions(int argc, char **argv) {
  Options options;
  int index = 1;
  if (index < argc && argv[index][0] != '-') {
    options.command = argv[index++];
  }
  while (index < argc) {
    const std::string_view option(argv[index++]);
    if (index >= argc) {
      throw std::invalid_argument("Missing value for " + std::string(option));
    }
    const std::string_view value(argv[index++]);
    if (option == "--database") {
      options.database = value;
    } else if (option == "--bind") {
      options.bind = value;
    } else if (option == "--port") {
      const auto port = parseInteger<unsigned>(value, option);
      if (port == 0U || port > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument("Port must be between 1 and 65535");
      }
      options.port = static_cast<std::uint16_t>(port);
    } else if (option == "--output") {
      options.output = value;
    } else if (option == "--ttl-seconds") {
      options.ttl_seconds = parseInteger<std::int64_t>(value, option);
    } else {
      throw std::invalid_argument("Unknown option: " + std::string(option));
    }
  }
  return options;
}

void printUsage() {
  std::cerr << "Usage:\n"
            << "  office_3ds_demo_server serve [--database PATH] [--bind ADDRESS] [--port PORT]\n"
            << "  office_3ds_demo_server reset [--database PATH]\n"
            << "  office_3ds_demo_server issue-token --output PATH [--database PATH] "
               "[--ttl-seconds SECONDS]\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    const auto options = parseOptions(argc, argv);
    if (options.command == "serve") {
      const auto listening =
        office3ds::demo::runServer({options.database, options.bind, options.port});
      if (!listening) {
        std::cerr << "The demo server could not bind its configured address.\n";
        return 1;
      }
      return 0;
    }

    office3ds::demo::DemoService service({options.database});
    if (options.command == "reset") {
      service.reset();
      std::cout << "Demo data was reset. Existing bearer credentials were revoked.\n";
      return 0;
    }
    if (options.command == "issue-token") {
      if (options.output.empty()) {
        throw std::invalid_argument("issue-token requires --output");
      }
      service.issueBearerToken(options.output, std::chrono::seconds(options.ttl_seconds));
      std::cout << "A bearer credential was written to the requested file.\n";
      return 0;
    }

    throw std::invalid_argument("Unknown command: " + options.command);
  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << '\n';
    printUsage();
    return 2;
  }
}
