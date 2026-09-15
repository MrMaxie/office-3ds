#include "office3ds/core/dashboard_state.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace office3ds::core {
namespace {

constexpr int kSubviewCount = 3;

std::size_t clamped_index(std::size_t index, int offset, std::size_t count) {
  if (count == 0) {
    return 0;
  }
  const auto shifted = static_cast<std::int64_t>(index) + offset;
  return static_cast<std::size_t>(
    std::clamp<std::int64_t>(shifted, 0, static_cast<std::int64_t>(count - 1)));
}

const api::ActivityEvent *selected_activity(const api::DashboardSnapshot &snapshot,
                                            std::size_t index) {
  return index < snapshot.activity.size() ? &snapshot.activity[index] : nullptr;
}

} // namespace

DashboardSubview DashboardNavigation::subview() const { return subview_; }

std::size_t DashboardNavigation::selected_index() const {
  switch (subview_) {
  case DashboardSubview::worklog:
    return worklog_index_;
  case DashboardSubview::absences:
    return absence_index_;
  case DashboardSubview::activity:
    return activity_index_;
  }
  return 0;
}

std::size_t DashboardNavigation::selected_recognition_index() const { return recognition_index_; }

std::optional<std::int32_t>
DashboardNavigation::selected_recognition_value(const api::DashboardSnapshot &snapshot) const {
  const auto *event = selected_activity(snapshot, activity_index_);
  return event != nullptr && recognition_index_ < event->allowed_recognition_values.size()
           ? std::optional<std::int32_t>{event->allowed_recognition_values[recognition_index_]}
           : std::nullopt;
}

RecognitionStatus DashboardNavigation::recognition_status() const { return recognition_status_; }

void DashboardNavigation::reset_for_snapshot(const api::DashboardSnapshot &snapshot) {
  worklog_index_ = snapshot.worklog.empty() ? 0 : snapshot.worklog.size() - 1;
  absence_index_ = 0;
  activity_index_ = 0;
  recognition_index_ = 0;
  recognition_status_ = RecognitionStatus::idle;
}

void DashboardNavigation::reconcile_snapshot(const api::DashboardSnapshot &snapshot) {
  worklog_index_ = clamped_index(worklog_index_, 0, snapshot.worklog.size());
  absence_index_ = clamped_index(absence_index_, 0, snapshot.absences.size());
  activity_index_ = clamped_index(activity_index_, 0, snapshot.activity.size());
  const auto *event = selected_activity(snapshot, activity_index_);
  recognition_index_ = clamped_index(
    recognition_index_, 0, event == nullptr ? 0 : event->allowed_recognition_values.size());
  recognition_status_ = RecognitionStatus::idle;
}

void DashboardNavigation::move_subview(int offset) {
  auto index = static_cast<int>(subview_);
  index = (index + offset % kSubviewCount + kSubviewCount) % kSubviewCount;
  select_subview(static_cast<DashboardSubview>(index));
}

void DashboardNavigation::select_subview(DashboardSubview subview) {
  subview_ = subview;
  recognition_status_ = RecognitionStatus::idle;
}

void DashboardNavigation::move_selection(int offset, const api::DashboardSnapshot &snapshot) {
  switch (subview_) {
  case DashboardSubview::worklog:
    worklog_index_ = clamped_index(worklog_index_, offset, snapshot.worklog.size());
    break;
  case DashboardSubview::absences:
    absence_index_ = clamped_index(absence_index_, offset, snapshot.absences.size());
    break;
  case DashboardSubview::activity:
    activity_index_ = clamped_index(activity_index_, offset, snapshot.activity.size());
    recognition_index_ = 0;
    recognition_status_ = RecognitionStatus::idle;
    break;
  }
}

void DashboardNavigation::move_recognition(int offset, const api::DashboardSnapshot &snapshot) {
  if (subview_ != DashboardSubview::activity || recognition_status_ == RecognitionStatus::pending) {
    return;
  }
  const auto *event = selected_activity(snapshot, activity_index_);
  recognition_index_ = clamped_index(
    recognition_index_, offset, event == nullptr ? 0 : event->allowed_recognition_values.size());
  recognition_status_ = RecognitionStatus::idle;
}

std::optional<api::RecognitionRequest>
DashboardNavigation::begin_recognition(const api::DashboardSnapshot &snapshot,
                                       std::string request_id) {
  const auto *event = selected_activity(snapshot, activity_index_);
  const auto value = selected_recognition_value(snapshot);
  if (subview_ != DashboardSubview::activity || recognition_status_ == RecognitionStatus::pending ||
      event == nullptr || event->id.empty() || event->recipient_id.empty() ||
      event->summary.empty() || request_id.empty() || !value.has_value()) {
    return std::nullopt;
  }
  recognition_status_ = RecognitionStatus::pending;
  return api::RecognitionRequest{event->id, event->recipient_id, *value, event->summary,
                                 std::move(request_id)};
}

void DashboardNavigation::mark_recognition_success() {
  if (recognition_status_ == RecognitionStatus::pending) {
    recognition_status_ = RecognitionStatus::success;
  }
}

void DashboardNavigation::mark_recognition_failure() {
  if (recognition_status_ == RecognitionStatus::pending) {
    recognition_status_ = RecognitionStatus::failure;
  }
}

ConnectionState DashboardState::connection_state() const { return connection_state_; }

const api::DashboardSnapshot *DashboardState::snapshot() const {
  return snapshot_.has_value() ? &*snapshot_ : nullptr;
}

bool DashboardState::has_snapshot() const { return snapshot_.has_value(); }

void DashboardState::set_snapshot(api::DashboardSnapshot snapshot) {
  snapshot_ = std::move(snapshot);
  mark_current();
}

void DashboardState::clear_snapshot() { snapshot_.reset(); }

void DashboardState::mark_current() { connection_state_ = ConnectionState::current; }

void DashboardState::mark_unpaired() { connection_state_ = ConnectionState::unpaired; }

void DashboardState::mark_pairing_failed() { connection_state_ = ConnectionState::pairing_failed; }

void DashboardState::mark_offline() { connection_state_ = ConnectionState::offline; }

} // namespace office3ds::core
