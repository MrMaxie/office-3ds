#pragma once

#include "office3ds/api/models.hpp"

#include <cstddef>
#include <optional>

namespace office3ds::core {

enum class DashboardSubview {
  worklog,
  absences,
  activity,
};

enum class RecognitionStatus {
  idle,
  pending,
  success,
  failure,
};

class DashboardNavigation {
public:
  [[nodiscard]] DashboardSubview subview() const;
  [[nodiscard]] std::size_t selected_index() const;
  [[nodiscard]] std::size_t selected_recognition_index() const;
  [[nodiscard]] std::optional<std::int32_t>
  selected_recognition_value(const api::DashboardSnapshot &snapshot) const;
  [[nodiscard]] RecognitionStatus recognition_status() const;

  void reset_for_snapshot(const api::DashboardSnapshot &snapshot);
  void reconcile_snapshot(const api::DashboardSnapshot &snapshot);
  void move_subview(int offset);
  void select_subview(DashboardSubview subview);
  void move_selection(int offset, const api::DashboardSnapshot &snapshot);
  void move_recognition(int offset, const api::DashboardSnapshot &snapshot);
  [[nodiscard]] std::optional<api::RecognitionRequest>
  begin_recognition(const api::DashboardSnapshot &snapshot, std::string request_id);
  void mark_recognition_success();
  void mark_recognition_failure();

private:
  DashboardSubview subview_ = DashboardSubview::worklog;
  std::size_t worklog_index_ = 0;
  std::size_t absence_index_ = 0;
  std::size_t activity_index_ = 0;
  std::size_t recognition_index_ = 0;
  RecognitionStatus recognition_status_ = RecognitionStatus::idle;
};

enum class ConnectionState {
  unpaired,
  pairing_failed,
  current,
  offline,
};

class DashboardState {
public:
  [[nodiscard]] ConnectionState connection_state() const;
  [[nodiscard]] const api::DashboardSnapshot *snapshot() const;
  [[nodiscard]] bool has_snapshot() const;

  void set_snapshot(api::DashboardSnapshot snapshot);
  void clear_snapshot();
  void mark_current();
  void mark_unpaired();
  void mark_pairing_failed();
  void mark_offline();

private:
  ConnectionState connection_state_ = ConnectionState::unpaired;
  std::optional<api::DashboardSnapshot> snapshot_;
};

} // namespace office3ds::core
