#include "office3ds/platform/resource_owner.hpp"

#include <utility>

namespace office3ds::platform {

ResourceOwner::~ResourceOwner() { (void)shutdown(LifecycleClock::now()); }

bool ResourceOwner::add(std::unique_ptr<RuntimeResource> resource) {
  if (stopped_ || !resource) {
    return false;
  }
  resources_.push_back(std::move(resource));
  return true;
}

std::size_t ResourceOwner::size() const noexcept { return resources_.size(); }

bool ResourceOwner::stopped() const noexcept { return stopped_; }

ShutdownReport ResourceOwner::shutdown(LifecycleClock::time_point deadline) noexcept {
  ShutdownReport report{resources_.size(), stopped_ ? resources_.size() : 0, false};
  if (stopped_) {
    return report;
  }
  stopped_ = true;

  for (auto resource = resources_.rbegin(); resource != resources_.rend(); ++resource) {
    (*resource)->request_stop();
  }
  for (auto resource = resources_.rbegin(); resource != resources_.rend(); ++resource) {
    if ((*resource)->shutdown_until(deadline)) {
      ++report.stopped_count;
    }
    if (LifecycleClock::now() > deadline) {
      report.deadline_exhausted = true;
    }
  }
  resources_.clear();
  return report;
}

} // namespace office3ds::platform
