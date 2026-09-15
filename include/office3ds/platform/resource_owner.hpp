#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <vector>

namespace office3ds::platform {

using LifecycleClock = std::chrono::steady_clock;

class RuntimeResource {
public:
  virtual ~RuntimeResource() = default;

  virtual void request_stop() noexcept = 0;
  // The resource must be safe to destroy after this returns, including when work exceeded deadline.
  [[nodiscard]] virtual bool shutdown_until(LifecycleClock::time_point deadline) noexcept = 0;
};

struct ShutdownReport {
  std::size_t resource_count = 0;
  std::size_t stopped_count = 0;
  bool deadline_exhausted = false;

  [[nodiscard]] bool complete() const noexcept {
    return stopped_count == resource_count && !deadline_exhausted;
  }
};

class ResourceOwner {
public:
  ResourceOwner() = default;
  ~ResourceOwner();

  ResourceOwner(const ResourceOwner &) = delete;
  ResourceOwner &operator=(const ResourceOwner &) = delete;

  [[nodiscard]] bool add(std::unique_ptr<RuntimeResource> resource);
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] bool stopped() const noexcept;
  [[nodiscard]] ShutdownReport shutdown(LifecycleClock::time_point deadline) noexcept;

private:
  std::vector<std::unique_ptr<RuntimeResource>> resources_;
  bool stopped_ = false;
};

} // namespace office3ds::platform
