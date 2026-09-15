#include <office3ds/demo/server.hpp>

#include <office3ds/demo/demo_service.hpp>

#include <httplib.h>

#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace office3ds::demo {
namespace {

constexpr std::size_t kMaximumRequestBytes = 4096U;

void copyResponse(const ApiResponse &source, httplib::Response &destination) {
  destination.status = source.status;
  destination.set_content(source.body, source.content_type);
  destination.set_header("Cache-Control", "no-store");
  destination.set_header("X-Content-Type-Options", "nosniff");
}

std::string authorization(const httplib::Request &request) {
  return request.has_header("Authorization") ? request.get_header_value("Authorization")
                                             : std::string{};
}

} // namespace

class DemoHttpServer::Impl {
public:
  explicit Impl(ServerConfig config)
      : config_(std::move(config)), service_(ServiceConfig{config_.database_path}) {
    service_.migrateAndSeed();
    server_.set_payload_max_length(kMaximumRequestBytes);
    server_.set_read_timeout(5, 0);
    server_.set_write_timeout(5, 0);
    server_.set_idle_interval(0, 100000);

    server_.Get("/health", [this](const httplib::Request &request, httplib::Response &response) {
      copyResponse(service_.handle("GET", request.path, "", ""), response);
    });

    const auto get = [this](const httplib::Request &request, httplib::Response &response) {
      copyResponse(service_.handle("GET", request.path, authorization(request), ""), response);
    };
    server_.Get("/v1/profile", get);
    server_.Get("/v1/worklog", get);
    server_.Get("/v1/absences", get);
    server_.Get("/v1/activity", get);
    server_.Get("/v1/recognitions", get);

    server_.Post(
      "/v1/recognitions", [this](const httplib::Request &request, httplib::Response &response) {
        copyResponse(service_.handle("POST", request.path, authorization(request), request.body),
                     response);
      });

    server_.set_error_handler([](const httplib::Request &, httplib::Response &response) {
      if (!response.body.empty()) {
        response.set_header("Cache-Control", "no-store");
        response.set_header("X-Content-Type-Options", "nosniff");
        return;
      }
      if (response.status == 413) {
        response.set_content(
          R"({"error":"request_too_large","message":"The request exceeds the server limit."})",
          "application/json");
      } else {
        response.set_content(
          R"({"error":"not_found","message":"The requested resource does not exist."})",
          "application/json");
      }
      response.set_header("Cache-Control", "no-store");
      response.set_header("X-Content-Type-Options", "nosniff");
    });
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

  bool listen() {
    if (bind() == 0U) {
      return false;
    }
    return server_.listen_after_bind();
  }

  void waitUntilReady() const { server_.wait_until_ready(); }

  void stop() { server_.stop(); }

private:
  ServerConfig config_;
  DemoService service_;
  httplib::Server server_;
  std::uint16_t bound_port_ = 0;
};

DemoHttpServer::DemoHttpServer(ServerConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

DemoHttpServer::~DemoHttpServer() = default;

std::uint16_t DemoHttpServer::bind() { return impl_->bind(); }

bool DemoHttpServer::listen() { return impl_->listen(); }

void DemoHttpServer::waitUntilReady() const { impl_->waitUntilReady(); }

void DemoHttpServer::stop() { impl_->stop(); }

bool runServer(const ServerConfig &config) {
  DemoHttpServer server(config);
  return server.listen();
}

} // namespace office3ds::demo
