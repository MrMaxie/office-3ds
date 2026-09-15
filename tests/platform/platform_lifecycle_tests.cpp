#include "office3ds/platform/credential.hpp"
#include "office3ds/platform/resource_owner.hpp"
#include "office3ds/platform/session.hpp"

#include <cassert>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace platform = office3ds::platform;

namespace {

class MemoryCredentialStore final : public platform::CredentialStore {
public:
  std::optional<office3ds::core::CredentialBundle> load() override { return credential; }

  bool save(const office3ds::core::CredentialBundle &value) override {
    ++save_count;
    credential = value;
    return save_succeeds;
  }

  void clear() noexcept override {
    ++clear_count;
    credential.reset();
  }

  std::optional<office3ds::core::CredentialBundle> credential;
  int save_count = 0;
  int clear_count = 0;
  bool save_succeeds = true;
};

class RecordingTransport final : public office3ds::api::HttpTransport {
public:
  std::optional<office3ds::api::ApiResponse>
  perform(const office3ds::api::HttpRequest &request) override {
    last_request = request;
    return response;
  }

  office3ds::api::HttpRequest last_request;
  std::optional<office3ds::api::ApiResponse> response = office3ds::api::ApiResponse{200, "{}"};
};

class OneStepDashboardLoad final : public office3ds::api::DashboardLoad {
public:
  explicit OneStepDashboardLoad(office3ds::api::AdapterResult result) : result_(result) {}

  office3ds::api::DashboardLoadUpdate
  advance(const office3ds::api::RequestExecutor &execute) override {
    const auto response = execute({"GET", "/v1/dashboard", {}, {}, {}, 4096});
    if (!response.has_value()) {
      return {office3ds::api::DashboardLoadStage::complete, 0,           1, true,
              office3ds::api::AdapterResult::unavailable,   std::nullopt};
    }
    return {office3ds::api::DashboardLoadStage::complete, 1, 1, true, result_,
            office3ds::api::DashboardSnapshot{}};
  }

private:
  office3ds::api::AdapterResult result_;
};

class SessionAdapter final : public office3ds::api::Adapter {
public:
  std::unique_ptr<office3ds::api::DashboardLoad>
  begin_dashboard_load(std::string_view today) override {
    last_date = today;
    return std::make_unique<OneStepDashboardLoad>(dashboard_result);
  }

  office3ds::api::RecognitionResult
  send_recognition(const office3ds::api::RecognitionRequest &request,
                   const office3ds::api::RequestExecutor &execute) override {
    const auto response = execute({"POST", "/v1/recognitions", {}, {}, request.message, 1024});
    if (!response.has_value()) {
      return {office3ds::api::AdapterResult::unavailable, false, {}, {}};
    }
    return {recognition_result,
            recognition_result == office3ds::api::AdapterResult::ok,
            request.request_id,
            {}};
  }

  std::string last_date;
  office3ds::api::AdapterResult dashboard_result = office3ds::api::AdapterResult::ok;
  office3ds::api::AdapterResult recognition_result = office3ds::api::AdapterResult::ok;
};

struct ClaimState {
  bool cancelled = false;
  std::size_t poll_count = 0;
};

class SequenceClaim final : public platform::CredentialClaimSource {
public:
  SequenceClaim(std::shared_ptr<ClaimState> state,
                std::vector<platform::CredentialClaimResult> results)
      : state_(std::move(state)), results_(std::move(results)) {}

  platform::CredentialClaimResult poll() override {
    const auto index = state_->poll_count++;
    return index < results_.size()
             ? results_[index]
             : platform::CredentialClaimResult{platform::CredentialClaimStatus::invalid_response,
                                               std::nullopt};
  }

  void cancel() noexcept override { state_->cancelled = true; }

private:
  std::shared_ptr<ClaimState> state_;
  std::vector<platform::CredentialClaimResult> results_;
};

class RecordingResource final : public platform::RuntimeResource {
public:
  RecordingResource(std::string name, std::vector<std::string> &events, bool shutdown_succeeds)
      : name_(std::move(name)), events_(events), shutdown_succeeds_(shutdown_succeeds) {}

  void request_stop() noexcept override { events_.push_back("request:" + name_); }

  bool shutdown_until(platform::LifecycleClock::time_point) noexcept override {
    events_.push_back("shutdown:" + name_);
    return shutdown_succeeds_;
  }

private:
  std::string name_;
  std::vector<std::string> &events_;
  bool shutdown_succeeds_;
};

void testCredentialPolicy() {
  assert(platform::CredentialPolicy::valid_api_origin("https://api.example.test"));
  assert(!platform::CredentialPolicy::valid_api_origin("https://api.example.test/"));
  assert(!platform::CredentialPolicy::valid_api_origin("https://api.example.test?q=value"));

  const auto usable = office3ds::core::CredentialBundle{"private-runtime-value", 2'000};
  assert(platform::CredentialPolicy::evaluate(usable, 1'000) == platform::CredentialStatus::usable);
  assert(platform::CredentialPolicy::evaluate(usable, 2'000) ==
         platform::CredentialStatus::expired);
  const auto skewed = office3ds::core::CredentialBundle{"private-runtime-value", 2'000, -7'200};
  assert(platform::CredentialPolicy::evaluate(skewed, 9'199) == platform::CredentialStatus::usable);
  assert(platform::CredentialPolicy::evaluate(skewed, 9'200) ==
         platform::CredentialStatus::expired);

  auto invalid = usable;
  invalid.access_token = "line\nbreak";
  assert(platform::CredentialPolicy::evaluate(invalid, 1'000) ==
         platform::CredentialStatus::invalid);
  invalid = usable;
  invalid.access_token.assign(platform::CredentialPolicy::maximum_bearer_size + 1, 'x');
  assert(platform::CredentialPolicy::evaluate(invalid, 1'000) ==
         platform::CredentialStatus::invalid);
}

void testStoredCredentialSession() {
  MemoryCredentialStore store;
  store.credential = office3ds::core::CredentialBundle{"runtime-value", 2'000};
  RecordingTransport transport;
  auto adapter = std::make_unique<SessionAdapter>();
  auto *adapter_view = adapter.get();
  platform::ProductSession session(transport, store, std::move(adapter),
                                   "https://api.example.test");

  assert(session.resume(1'000) == platform::CredentialStatus::usable);
  assert(session.state() == platform::SessionState::ready);
  assert(session.begin_dashboard_load("2026-01-05", 1'000) == office3ds::api::AdapterResult::ok);
  const auto update = session.advance_dashboard_load(1'000);
  assert(update.finished && update.result == office3ds::api::AdapterResult::ok);
  assert(session.state() == platform::SessionState::current);
  assert(adapter_view->last_date == "2026-01-05");
  assert(transport.last_request.url == "https://api.example.test/v1/dashboard");
  assert(transport.last_request.headers.size() == 1);
  assert(transport.last_request.headers.front().first == "Authorization");
  assert(transport.last_request.max_response_size == 4096);

  const auto expired =
    session.send_recognition({"activity-1", "person-1", 3, "Thank you", "request-1"}, 2'000);
  assert(expired.result == office3ds::api::AdapterResult::unauthorized);
  assert(session.state() == platform::SessionState::expired);
  assert(!store.credential.has_value() && store.clear_count == 1);
}

void testCredentialClaimAndShutdown() {
  const auto credential = office3ds::core::CredentialBundle{"claimed-value", 3'000};
  MemoryCredentialStore store;
  RecordingTransport transport;
  platform::ProductSession session(transport, store, std::make_unique<SessionAdapter>(),
                                   "https://api.example.test");

  auto completed_state = std::make_shared<ClaimState>();
  auto completed_claim = std::make_unique<SequenceClaim>(
    completed_state, std::vector<platform::CredentialClaimResult>{
                       {platform::CredentialClaimStatus::pending, std::nullopt},
                       {platform::CredentialClaimStatus::accepted, credential},
                     });
  assert(session.begin_credential_claim(std::move(completed_claim)));
  assert(session.advance_credential_claim(1'000) == platform::CredentialClaimStatus::pending);
  assert(session.advance_credential_claim(1'000) == platform::CredentialClaimStatus::accepted);
  assert(session.state() == platform::SessionState::ready);
  assert(store.save_count == 1 && store.credential.has_value());
  assert(!completed_state->cancelled);

  auto cancelled_state = std::make_shared<ClaimState>();
  assert(session.begin_credential_claim(std::make_unique<SequenceClaim>(
    cancelled_state, std::vector<platform::CredentialClaimResult>{})));
  session.shutdown();
  assert(cancelled_state->cancelled);
  assert(session.state() == platform::SessionState::stopped);
  assert(store.credential.has_value());
}

void testBoundedResourceOwnership() {
  std::vector<std::string> events;
  platform::ResourceOwner resources;
  assert(resources.add(std::make_unique<RecordingResource>("network", events, true)));
  assert(resources.add(std::make_unique<RecordingResource>("graphics", events, false)));
  assert(resources.size() == 2);

  const auto report = resources.shutdown(platform::LifecycleClock::now() + std::chrono::seconds(1));
  const std::vector<std::string> expected{"request:graphics", "request:network",
                                          "shutdown:graphics", "shutdown:network"};
  assert(events == expected);
  assert(report.resource_count == 2 && report.stopped_count == 1 && !report.complete());
  assert(resources.stopped() && resources.size() == 0);
  assert(!resources.add(std::make_unique<RecordingResource>("late", events, true)));

  const auto repeated = resources.shutdown(platform::LifecycleClock::now());
  assert(repeated.complete() && repeated.resource_count == 0);
  assert(events == expected);
}

} // namespace

int main() {
  testCredentialPolicy();
  testStoredCredentialSession();
  testCredentialClaimAndShutdown();
  testBoundedResourceOwnership();
  return 0;
}
