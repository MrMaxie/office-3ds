#include <office3ds/demo/demo_service.hpp>

#include "sha256.hpp"

#include <sqlite3.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
// clang-format off
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
// clang-format on
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace office3ds::demo {
namespace {

using Json = nlohmann::json;

constexpr int kSchemaVersion = 3;
constexpr std::size_t kMaximumRequestBytes = 4096U;
constexpr std::size_t kMaximumIdentifierBytes = 64U;
constexpr std::size_t kMaximumCommentBytes = 280U;

class Statement {
public:
  Statement(sqlite3 *database, const char *sql) : database_(database) {
    if (sqlite3_prepare_v2(database, sql, -1, &statement_, nullptr) != SQLITE_OK) {
      throw std::runtime_error(sqlite3_errmsg(database));
    }
  }

  ~Statement() { sqlite3_finalize(statement_); }
  Statement(const Statement &) = delete;
  Statement &operator=(const Statement &) = delete;

  sqlite3_stmt *get() const { return statement_; }

  void bindText(int index, std::string_view value) {
    if (sqlite3_bind_text(statement_, index, value.data(), static_cast<int>(value.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
      throw std::runtime_error(sqlite3_errmsg(database_));
    }
  }

  void bindInt64(int index, std::int64_t value) {
    if (sqlite3_bind_int64(statement_, index, value) != SQLITE_OK) {
      throw std::runtime_error(sqlite3_errmsg(database_));
    }
  }

  bool stepRow() {
    const auto result = sqlite3_step(statement_);
    if (result == SQLITE_ROW) {
      return true;
    }
    if (result == SQLITE_DONE) {
      return false;
    }
    throw std::runtime_error(sqlite3_errmsg(database_));
  }

  void execute() {
    if (sqlite3_step(statement_) != SQLITE_DONE) {
      throw std::runtime_error(sqlite3_errmsg(database_));
    }
  }

private:
  sqlite3 *database_{nullptr};
  sqlite3_stmt *statement_{nullptr};
};

void execute(sqlite3 *database, const char *sql) {
  char *error = nullptr;
  if (sqlite3_exec(database, sql, nullptr, nullptr, &error) != SQLITE_OK) {
    const std::string message = error == nullptr ? "SQLite operation failed" : error;
    sqlite3_free(error);
    throw std::runtime_error(message);
  }
}

class Transaction {
public:
  explicit Transaction(sqlite3 *database) : database_(database) {
    execute(database_, "BEGIN IMMEDIATE");
  }
  ~Transaction() {
    if (!committed_) {
      sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
    }
  }
  Transaction(const Transaction &) = delete;
  Transaction &operator=(const Transaction &) = delete;

  void commit() {
    execute(database_, "COMMIT");
    committed_ = true;
  }

private:
  sqlite3 *database_;
  bool committed_{false};
};

std::int64_t currentEpochSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
           std::chrono::system_clock::now().time_since_epoch())
    .count();
}

const char *columnText(sqlite3_stmt *statement, int column) {
  const auto *value = sqlite3_column_text(statement, column);
  return value == nullptr ? "" : reinterpret_cast<const char *>(value);
}

ApiResponse jsonResponse(int status, Json body) {
  return ApiResponse{status, "application/json", body.dump()};
}

ApiResponse errorResponse(int status, std::string_view code, std::string_view message) {
  return jsonResponse(status, Json{{"error", code}, {"message", message}});
}

bool validIdentifier(std::string_view value) {
  if (value.empty() || value.size() > kMaximumIdentifierBytes) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return std::isalnum(character) != 0 || character == '-' || character == '_';
  });
}

bool constantTimeEquals(std::string_view left, std::string_view right) {
  const auto maximum = std::max(left.size(), right.size());
  std::uint8_t difference = left.size() == right.size() ? 0U : 1U;
  for (std::size_t index = 0; index < maximum; ++index) {
    const auto left_byte = index < left.size() ? left[index] : 0;
    const auto right_byte = index < right.size() ? right[index] : 0;
    difference |= static_cast<std::uint8_t>(left_byte ^ right_byte);
  }
  return difference == 0U;
}

std::string createToken() {
  std::array<std::uint8_t, 32> bytes{};
#ifdef _WIN32
  if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    throw std::runtime_error("The operating system random source is unavailable");
  }
#else
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto received = ::getrandom(bytes.data() + offset, bytes.size() - offset, 0);
    if (received < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error("The operating system random source is unavailable");
    }
    offset += static_cast<std::size_t>(received);
  }
#endif
  return "office_demo_" + detail::hexEncode(bytes);
}

void writePrivateFile(const std::string &path, std::string_view contents) {
  if (path.empty()) {
    throw std::invalid_argument("Token output path must not be empty");
  }
  const std::string payload = std::string(contents) + "\n";
#ifdef _WIN32
  const auto destination = std::filesystem::path(path).wstring();
  PSECURITY_DESCRIPTOR security_descriptor = nullptr;
  if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;OW)", SDDL_REVISION_1,
                                                           &security_descriptor, nullptr) == 0) {
    throw std::runtime_error("Could not create private token file permissions");
  }
  SECURITY_ATTRIBUTES security_attributes{};
  security_attributes.nLength = sizeof(security_attributes);
  security_attributes.lpSecurityDescriptor = security_descriptor;
  HANDLE file = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, &security_attributes, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  LocalFree(security_descriptor);
  if (file == INVALID_HANDLE_VALUE) {
    throw std::runtime_error("Could not create a new private token file");
  }
  DWORD written = 0;
  const auto ok =
    WriteFile(file, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr) != 0 &&
    written == payload.size() && FlushFileBuffers(file) != 0;
  CloseHandle(file);
  if (!ok) {
    std::filesystem::remove(path);
    throw std::runtime_error("Could not write the private token file");
  }
#else
  const auto file =
    ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, S_IRUSR | S_IWUSR);
  if (file < 0) {
    throw std::runtime_error("Could not create a new private token file");
  }
  std::size_t offset = 0;
  while (offset < payload.size()) {
    const auto written = ::write(file, payload.data() + offset, payload.size() - offset);
    if (written < 0) {
      const auto saved_errno = errno;
      ::close(file);
      ::unlink(path.c_str());
      throw std::runtime_error("Could not write the private token file: " +
                               std::to_string(saved_errno));
    }
    offset += static_cast<std::size_t>(written);
  }
  if (::fsync(file) != 0 || ::close(file) != 0) {
    ::unlink(path.c_str());
    throw std::runtime_error("Could not finalize the private token file");
  }
#endif
}

bool allowedRecognitionValue(std::int64_t value) { return value == 1 || value == 3 || value == 5; }

} // namespace

class DemoService::Impl {
public:
  explicit Impl(ServiceConfig config) : config_(std::move(config)) {
    if (config_.database_path.empty()) {
      throw std::invalid_argument("Database path must not be empty");
    }
    if (sqlite3_open_v2(config_.database_path.c_str(), &database_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
      const std::string message =
        database_ == nullptr ? "Could not open SQLite database" : sqlite3_errmsg(database_);
      sqlite3_close(database_);
      database_ = nullptr;
      throw std::runtime_error(message);
    }
    sqlite3_busy_timeout(database_, 5000);
    execute(database_, "PRAGMA journal_mode=WAL");
    execute(database_, "PRAGMA foreign_keys=ON");
  }

  ~Impl() { sqlite3_close(database_); }

  void migrateAndSeed() {
    std::scoped_lock lock(mutex_);
    migrateAndSeedLocked();
  }

  void reset() {
    std::scoped_lock lock(mutex_);
    migrateAndSeedLocked();
    Transaction transaction(database_);
    execute(database_, "DELETE FROM recognitions");
    execute(database_, "DELETE FROM token_credentials");
    execute(database_, "DELETE FROM activity_events");
    execute(database_, "DELETE FROM absences");
    execute(database_, "DELETE FROM worklog_days");
    execute(database_, "DELETE FROM profiles");
    seedLocked();
    transaction.commit();
  }

  void issueBearerToken(const std::string &output_path, std::chrono::seconds lifetime) {
    if (lifetime <= std::chrono::seconds::zero() || lifetime > std::chrono::hours(24)) {
      throw std::invalid_argument("Token lifetime must be between 1 second and 24 hours");
    }
    const auto token = createToken();
    const auto hash = detail::hexEncode(detail::sha256(token));
    const auto expires_at = currentEpochSeconds() + lifetime.count();

    std::scoped_lock lock(mutex_);
    migrateAndSeedLocked();
    writePrivateFile(output_path, Json{{"access_token", token}, {"expires_at", expires_at}}.dump());
    try {
      Statement insert(database_,
                       "INSERT INTO token_credentials(token_hash, expires_at, created_at) "
                       "VALUES(?1, ?2, ?3)");
      insert.bindText(1, hash);
      insert.bindInt64(2, expires_at);
      insert.bindInt64(3, currentEpochSeconds());
      insert.execute();
    } catch (...) {
      std::error_code ignored;
      std::filesystem::remove(output_path, ignored);
      throw;
    }
  }

  ApiResponse handle(std::string_view method, std::string_view path, std::string_view authorization,
                     std::string_view request_body, std::int64_t now_epoch_seconds) {
    try {
      std::scoped_lock lock(mutex_);
      migrateAndSeedLocked();

      if (method == "GET" && path == "/health") {
        return jsonResponse(200, Json{{"status", "ok"}, {"schema_version", kSchemaVersion}});
      }
      const auto now = now_epoch_seconds == 0 ? currentEpochSeconds() : now_epoch_seconds;
      if (!isAuthorizedLocked(authorization, now)) {
        return errorResponse(401, "unauthorized", "A valid bearer credential is required.");
      }

      if (method == "GET" && path == "/v1/profile") {
        return profileLocked();
      }
      if (method == "GET" && path == "/v1/worklog") {
        return worklogLocked();
      }
      if (method == "GET" && path == "/v1/absences") {
        return absencesLocked();
      }
      if (method == "GET" && path == "/v1/activity") {
        return activityLocked();
      }
      if (method == "GET" && path == "/v1/recognitions") {
        return recognitionsLocked();
      }
      if (method == "POST" && path == "/v1/recognitions") {
        return createRecognitionLocked(request_body, now);
      }
      return errorResponse(404, "not_found", "The requested resource does not exist.");
    } catch (const std::exception &) {
      return errorResponse(500, "internal_error",
                           "The demo service could not complete the request.");
    }
  }

private:
  void migrateAndSeedLocked() {
    if (initialized_) {
      return;
    }
    Transaction transaction(database_);
    execute(database_, "CREATE TABLE IF NOT EXISTS schema_version("
                       "id INTEGER PRIMARY KEY CHECK(id = 1), version INTEGER NOT NULL)");
    execute(database_, "INSERT OR IGNORE INTO schema_version(id, version) VALUES(1, 0)");

    int version = 0;
    {
      Statement version_query(database_, "SELECT version FROM schema_version WHERE id = 1");
      if (!version_query.stepRow()) {
        throw std::runtime_error("Schema version record is missing");
      }
      version = sqlite3_column_int(version_query.get(), 0);
    }
    if (version > kSchemaVersion) {
      throw std::runtime_error("Database schema is newer than this server");
    }
    if (version < 1) {
      execute(database_, "CREATE TABLE profiles("
                         "id TEXT PRIMARY KEY, display_name TEXT NOT NULL, role TEXT NOT NULL, "
                         "team TEXT NOT NULL)");
      execute(database_, "CREATE TABLE worklog_days("
                         "id INTEGER PRIMARY KEY, work_date TEXT NOT NULL UNIQUE, "
                         "minutes INTEGER NOT NULL CHECK(minutes BETWEEN 0 AND 1440), "
                         "summary TEXT NOT NULL)");
      execute(database_, "CREATE TABLE absences("
                         "id INTEGER PRIMARY KEY, starts_on TEXT NOT NULL, ends_on TEXT NOT NULL, "
                         "kind TEXT NOT NULL, status TEXT NOT NULL)");
      execute(database_, "CREATE TABLE activity_events("
                         "id INTEGER PRIMARY KEY, occurred_at TEXT NOT NULL, kind TEXT NOT NULL, "
                         "summary TEXT NOT NULL)");
      execute(database_, "CREATE TABLE recognitions("
                         "id INTEGER PRIMARY KEY AUTOINCREMENT, recipient_id TEXT NOT NULL, "
                         "message TEXT NOT NULL, created_at INTEGER NOT NULL)");
      execute(database_, "CREATE TABLE token_credentials("
                         "token_hash TEXT PRIMARY KEY, expires_at INTEGER NOT NULL, "
                         "created_at INTEGER NOT NULL)");
      execute(database_, "UPDATE schema_version SET version = 1 WHERE id = 1");
    }
    if (version < 2) {
      execute(database_, "ALTER TABLE recognitions RENAME TO recognitions_v1");
      execute(database_,
              "CREATE TABLE recognitions("
              "id INTEGER PRIMARY KEY AUTOINCREMENT, "
              "request_id TEXT NOT NULL UNIQUE, "
              "source_event_id INTEGER NOT NULL REFERENCES activity_events(id), "
              "recipient_id TEXT NOT NULL REFERENCES profiles(id), "
              "value TEXT NOT NULL CHECK(value IN ('thank_you', 'great_work', 'helpful')), "
              "comment TEXT NOT NULL DEFAULT '' CHECK(length(comment) <= 280), "
              "created_at INTEGER NOT NULL)");
      execute(database_,
              "INSERT INTO recognitions(request_id, source_event_id, recipient_id, value, "
              "comment, created_at) "
              "SELECT 'migrated-' || r.id, 1, r.recipient_id, 'thank_you', r.message, "
              "r.created_at FROM recognitions_v1 r "
              "JOIN profiles p ON p.id = r.recipient_id "
              "JOIN activity_events a ON a.id = 1 "
              "WHERE length(r.message) <= 280");
      execute(database_, "DROP TABLE recognitions_v1");
      execute(database_, "UPDATE schema_version SET version = 2 WHERE id = 1");
    }
    if (version < 3) {
      execute(database_, "ALTER TABLE worklog_days ADD COLUMN expected_minutes INTEGER NOT NULL "
                         "DEFAULT 480 CHECK(expected_minutes BETWEEN 0 AND 1440)");
      execute(database_, "ALTER TABLE activity_events ADD COLUMN recipient_id TEXT");
      execute(database_, "UPDATE activity_events SET recipient_id = "
                         "(SELECT id FROM profiles ORDER BY id LIMIT 1) "
                         "WHERE recipient_id IS NULL");
      execute(database_, "ALTER TABLE recognitions RENAME TO recognitions_v2");
      execute(database_, "CREATE TABLE recognitions("
                         "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                         "request_id TEXT NOT NULL UNIQUE, "
                         "source_event_id INTEGER NOT NULL REFERENCES activity_events(id), "
                         "recipient_id TEXT NOT NULL REFERENCES profiles(id), "
                         "value INTEGER NOT NULL CHECK(value IN (1, 3, 5)), "
                         "comment TEXT NOT NULL DEFAULT '' CHECK(length(comment) <= 280), "
                         "created_at INTEGER NOT NULL)");
      execute(database_,
              "INSERT INTO recognitions(id, request_id, source_event_id, recipient_id, value, "
              "comment, created_at) "
              "SELECT id, request_id, source_event_id, recipient_id, "
              "CASE value WHEN 'helpful' THEN 1 WHEN 'great_work' THEN 3 ELSE 5 END, "
              "comment, created_at FROM recognitions_v2");
      execute(database_, "DROP TABLE recognitions_v2");
      execute(database_, "UPDATE schema_version SET version = 3 WHERE id = 1");
    }
    seedLocked();
    transaction.commit();
    initialized_ = true;
  }

  void seedLocked() {
    execute(database_, "INSERT OR IGNORE INTO profiles(id, display_name, role, team) VALUES"
                       "('alex-rivera', 'Alex Rivera', 'Workspace coordinator', 'North Studio')");
    execute(database_, "INSERT OR IGNORE INTO worklog_days("
                       "id, work_date, minutes, summary, expected_minutes) VALUES"
                       "(1, '2026-06-15', 450, 'Planning and customer research', 480),"
                       "(2, '2026-06-16', 480, 'Prototype implementation', 480),"
                       "(3, '2026-06-17', 420, 'Review and documentation', 480)");
    execute(database_, "INSERT OR IGNORE INTO absences(id, starts_on, ends_on, kind, status) VALUES"
                       "(1, '2026-07-06', '2026-07-10', 'time_off', 'approved')");
    execute(database_, "INSERT OR IGNORE INTO activity_events("
                       "id, occurred_at, kind, summary, recipient_id) VALUES"
                       "(1, '2026-06-17T15:30:00Z', 'worklog_updated', 'Worklog was updated', "
                       "'alex-rivera'),"
                       "(2, '2026-06-17T12:00:00Z', 'absence_approved', 'Time off was approved', "
                       "'alex-rivera')");
  }

  bool isAuthorizedLocked(std::string_view authorization, std::int64_t now) {
    constexpr std::string_view prefix = "Bearer ";
    if (authorization.size() <= prefix.size() || authorization.substr(0, prefix.size()) != prefix) {
      return false;
    }
    const auto supplied_hash =
      detail::hexEncode(detail::sha256(authorization.substr(prefix.size())));
    Statement query(database_, "SELECT token_hash FROM token_credentials WHERE expires_at > ?1");
    query.bindInt64(1, now);
    while (query.stepRow()) {
      if (constantTimeEquals(supplied_hash, columnText(query.get(), 0))) {
        return true;
      }
    }
    return false;
  }

  ApiResponse profileLocked() {
    Statement query(database_,
                    "SELECT id, display_name, role, team FROM profiles ORDER BY id LIMIT 1");
    if (!query.stepRow()) {
      return errorResponse(404, "not_found", "The demo profile is not available.");
    }
    return jsonResponse(200, Json{{"id", columnText(query.get(), 0)},
                                  {"display_name", columnText(query.get(), 1)},
                                  {"role", columnText(query.get(), 2)},
                                  {"team", columnText(query.get(), 3)}});
  }

  ApiResponse worklogLocked() {
    Json items = Json::array();
    Statement query(database_, "SELECT work_date, minutes, expected_minutes, summary "
                               "FROM worklog_days ORDER BY work_date");
    while (query.stepRow()) {
      items.push_back(Json{{"date", columnText(query.get(), 0)},
                           {"minutes", sqlite3_column_int(query.get(), 1)},
                           {"expected_minutes", sqlite3_column_int(query.get(), 2)},
                           {"summary", columnText(query.get(), 3)}});
    }
    return jsonResponse(200, Json{{"items", std::move(items)}});
  }

  ApiResponse absencesLocked() {
    Json items = Json::array();
    Statement query(database_, "SELECT a.id, p.display_name, a.starts_on, a.ends_on, "
                               "a.kind, a.status FROM absences a CROSS JOIN profiles p "
                               "ORDER BY a.starts_on");
    while (query.stepRow()) {
      items.push_back(Json{{"id", std::to_string(sqlite3_column_int64(query.get(), 0))},
                           {"person_name", columnText(query.get(), 1)},
                           {"starts_on", columnText(query.get(), 2)},
                           {"ends_on", columnText(query.get(), 3)},
                           {"return_date", columnText(query.get(), 3)},
                           {"label", columnText(query.get(), 4)},
                           {"group", ""},
                           {"status", columnText(query.get(), 5)}});
    }
    return jsonResponse(200, Json{{"items", std::move(items)}});
  }

  ApiResponse activityLocked() {
    Json items = Json::array();
    Statement query(database_, "SELECT a.id, a.recipient_id, p.display_name, a.occurred_at, "
                               "a.kind, a.summary FROM activity_events a "
                               "JOIN profiles p ON p.id = a.recipient_id "
                               "ORDER BY a.occurred_at DESC");
    while (query.stepRow()) {
      items.push_back(Json{{"id", std::to_string(sqlite3_column_int64(query.get(), 0))},
                           {"recipient_id", columnText(query.get(), 1)},
                           {"recipient_name", columnText(query.get(), 2)},
                           {"occurred_at", columnText(query.get(), 3)},
                           {"kind", columnText(query.get(), 4)},
                           {"summary", columnText(query.get(), 5)},
                           {"allowed_recognition_values", Json::array({5, 3, 1})}});
    }
    return jsonResponse(200, Json{{"items", std::move(items)}});
  }

  ApiResponse recognitionsLocked() {
    Json items = Json::array();
    Statement query(database_,
                    "SELECT id, request_id, source_event_id, recipient_id, value, comment, "
                    "created_at FROM recognitions "
                    "ORDER BY id DESC LIMIT 100");
    while (query.stepRow()) {
      items.push_back(Json{{"id", sqlite3_column_int64(query.get(), 0)},
                           {"request_id", columnText(query.get(), 1)},
                           {"source_event_id", sqlite3_column_int64(query.get(), 2)},
                           {"recipient_id", columnText(query.get(), 3)},
                           {"value", sqlite3_column_int(query.get(), 4)},
                           {"comment", columnText(query.get(), 5)},
                           {"created_at", sqlite3_column_int64(query.get(), 6)}});
    }
    return jsonResponse(200, Json{{"items", std::move(items)}});
  }

  ApiResponse createRecognitionLocked(std::string_view body, std::int64_t now) {
    if (body.empty() || body.size() > kMaximumRequestBytes) {
      return errorResponse(413, "request_too_large",
                           "Recognition requests must be between 1 and 4096 bytes.");
    }
    Json document;
    try {
      document = Json::parse(body);
    } catch (const Json::exception &) {
      return errorResponse(400, "invalid_json", "The request body must be valid JSON.");
    }
    if (!document.is_object() || !document.contains("request_id") ||
        !document["request_id"].is_string() || !document.contains("source_event_id") ||
        !document["source_event_id"].is_number_integer() || !document.contains("recipient_id") ||
        !document["recipient_id"].is_string() || !document.contains("value") ||
        !document["value"].is_number_integer() ||
        (document.contains("comment") && !document["comment"].is_string())) {
      return errorResponse(422, "invalid_recognition",
                           "The recognition fields have invalid types.");
    }
    const auto request_id = document["request_id"].get<std::string>();
    const auto source_event_id = document["source_event_id"].get<std::int64_t>();
    const auto recipient_id = document["recipient_id"].get<std::string>();
    const auto value = document["value"].get<std::int64_t>();
    const auto comment = document.value("comment", std::string{});
    if (!validIdentifier(request_id) || !validIdentifier(recipient_id) || source_event_id <= 0 ||
        !allowedRecognitionValue(value) || comment.size() > kMaximumCommentBytes) {
      return errorResponse(422, "invalid_recognition",
                           "Recognition fields are outside their allowed bounds.");
    }

    Statement existing(database_,
                       "SELECT source_event_id, recipient_id, value, comment, created_at, id "
                       "FROM recognitions WHERE request_id = ?1");
    existing.bindText(1, request_id);
    if (existing.stepRow()) {
      const auto same = sqlite3_column_int64(existing.get(), 0) == source_event_id &&
                        recipient_id == columnText(existing.get(), 1) &&
                        value == sqlite3_column_int64(existing.get(), 2) &&
                        comment == columnText(existing.get(), 3);
      if (!same) {
        return errorResponse(409, "request_id_conflict",
                             "request_id was already used for another recognition.");
      }
      return jsonResponse(200, Json{{"id", sqlite3_column_int64(existing.get(), 5)},
                                    {"request_id", request_id},
                                    {"source_event_id", source_event_id},
                                    {"recipient_id", recipient_id},
                                    {"value", value},
                                    {"comment", comment},
                                    {"created_at", sqlite3_column_int64(existing.get(), 4)}});
    }

    Statement source(database_, "SELECT 1 FROM activity_events WHERE id = ?1");
    source.bindInt64(1, source_event_id);
    if (!source.stepRow()) {
      return errorResponse(422, "unknown_source_event",
                           "source_event_id does not identify an activity event.");
    }
    Statement recipient(database_, "SELECT 1 FROM profiles WHERE id = ?1");
    recipient.bindText(1, recipient_id);
    if (!recipient.stepRow()) {
      return errorResponse(422, "unknown_recipient", "recipient_id does not identify a profile.");
    }

    Transaction transaction(database_);
    Statement insert(database_,
                     "INSERT INTO recognitions(request_id, source_event_id, recipient_id, "
                     "value, comment, created_at) VALUES(?1, ?2, ?3, ?4, ?5, ?6)");
    insert.bindText(1, request_id);
    insert.bindInt64(2, source_event_id);
    insert.bindText(3, recipient_id);
    insert.bindInt64(4, value);
    insert.bindText(5, comment);
    insert.bindInt64(6, now);
    insert.execute();
    const auto id = sqlite3_last_insert_rowid(database_);
    transaction.commit();
    return jsonResponse(201, Json{{"id", id},
                                  {"request_id", request_id},
                                  {"source_event_id", source_event_id},
                                  {"recipient_id", recipient_id},
                                  {"value", value},
                                  {"comment", comment},
                                  {"created_at", now}});
  }

  ServiceConfig config_;
  sqlite3 *database_{nullptr};
  std::mutex mutex_;
  bool initialized_{false};
};

DemoService::DemoService(ServiceConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
DemoService::~DemoService() = default;
DemoService::DemoService(DemoService &&) noexcept = default;
DemoService &DemoService::operator=(DemoService &&) noexcept = default;

void DemoService::migrateAndSeed() { impl_->migrateAndSeed(); }
void DemoService::reset() { impl_->reset(); }
void DemoService::issueBearerToken(const std::string &output_path, std::chrono::seconds lifetime) {
  impl_->issueBearerToken(output_path, lifetime);
}
ApiResponse DemoService::handle(std::string_view method, std::string_view path,
                                std::string_view authorization, std::string_view request_body,
                                std::int64_t now_epoch_seconds) {
  return impl_->handle(method, path, authorization, request_body, now_epoch_seconds);
}

} // namespace office3ds::demo
