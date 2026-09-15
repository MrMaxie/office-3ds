#include <office3ds/demo/demo_service.hpp>

#include <sqlite3.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

namespace {

using office3ds::demo::DemoService;
using Json = nlohmann::json;

class TemporaryDirectory {
public:
  TemporaryDirectory() {
    std::random_device random;
    path_ = std::filesystem::temp_directory_path() /
            ("office-3ds-demo-tests-" + std::to_string(random()));
    std::filesystem::create_directories(path_);
  }
  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  require(input.good(), "Expected file to be readable: " + path.string());
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string trimNewline(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
    value.pop_back();
  }
  return value;
}

std::string readToken(const std::filesystem::path &path) {
  const auto credential = Json::parse(trimNewline(readFile(path)));
  require(credential.is_object() && credential.size() == 2U &&
            credential.at("access_token").is_string() &&
            credential.at("expires_at").is_number_integer(),
          "Credential file should contain a token and authoritative expiry");
  return credential.at("access_token").get<std::string>();
}

void verifySchemaVersion(const std::filesystem::path &database_path) {
  sqlite3 *database = nullptr;
  require(sqlite3_open(database_path.string().c_str(), &database) == SQLITE_OK,
          "Could not inspect test database");
  sqlite3_stmt *statement = nullptr;
  require(sqlite3_prepare_v2(database, "SELECT version FROM schema_version WHERE id = 1", -1,
                             &statement, nullptr) == SQLITE_OK,
          "Could not query schema version");
  require(sqlite3_step(statement) == SQLITE_ROW, "Schema version row is missing");
  require(sqlite3_column_int(statement, 0) == 3, "Unexpected schema version");
  sqlite3_finalize(statement);
  sqlite3_close(database);
}

void executeSql(sqlite3 *database, const char *sql) {
  char *error = nullptr;
  if (sqlite3_exec(database, sql, nullptr, nullptr, &error) != SQLITE_OK) {
    const std::string message = error == nullptr ? "SQLite operation failed" : error;
    sqlite3_free(error);
    throw std::runtime_error(message);
  }
}

void testVersionOneMigration(const std::filesystem::path &directory) {
  const auto database_path = directory / "version-one.sqlite3";
  sqlite3 *database = nullptr;
  require(sqlite3_open(database_path.string().c_str(), &database) == SQLITE_OK,
          "Could not create version-one database");
  executeSql(
    database,
    "BEGIN;"
    "CREATE TABLE schema_version(id INTEGER PRIMARY KEY, version INTEGER NOT NULL);"
    "INSERT INTO schema_version VALUES(1, 1);"
    "CREATE TABLE profiles(id TEXT PRIMARY KEY, display_name TEXT NOT NULL, "
    "role TEXT NOT NULL, team TEXT NOT NULL);"
    "INSERT INTO profiles VALUES('alex-rivera','Alex Rivera','Coordinator','North Studio');"
    "CREATE TABLE worklog_days(id INTEGER PRIMARY KEY, work_date TEXT NOT NULL UNIQUE, "
    "minutes INTEGER NOT NULL, summary TEXT NOT NULL);"
    "CREATE TABLE absences(id INTEGER PRIMARY KEY, starts_on TEXT NOT NULL, "
    "ends_on TEXT NOT NULL, kind TEXT NOT NULL, status TEXT NOT NULL);"
    "CREATE TABLE activity_events(id INTEGER PRIMARY KEY, occurred_at TEXT NOT NULL, "
    "kind TEXT NOT NULL, summary TEXT NOT NULL);"
    "INSERT INTO activity_events VALUES(1,'2026-06-17T12:00:00Z','reviewed','Reviewed');"
    "CREATE TABLE recognitions(id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "recipient_id TEXT NOT NULL, message TEXT NOT NULL, created_at INTEGER NOT NULL);"
    "INSERT INTO recognitions(recipient_id,message,created_at) "
    "VALUES('alex-rivera','Legacy recognition',1781711000);"
    "CREATE TABLE token_credentials(token_hash TEXT PRIMARY KEY, "
    "expires_at INTEGER NOT NULL, created_at INTEGER NOT NULL);"
    "COMMIT;");
  sqlite3_close(database);

  const auto credential_file = directory / "migration-token.txt";
  DemoService service({database_path.string()});
  service.migrateAndSeed();
  verifySchemaVersion(database_path);
  service.issueBearerToken(credential_file.string(), std::chrono::hours(1));
  const auto authorization = "Bearer " + readToken(credential_file);
  const auto listed = service.handle("GET", "/v1/recognitions", authorization, "");
  require(listed.status == 200, "Migrated recognitions should be readable");
  const auto items = Json::parse(listed.body).at("items");
  require(items.size() == 1U && items.at(0).at("request_id") == "migrated-1",
          "Version-one recognition should migrate to the idempotent contract");
}

void testMigrationSeedAndAuthentication(const std::filesystem::path &directory) {
  const auto database = directory / "migration.sqlite3";
  const auto credential_file = directory / "credential.txt";
  DemoService service({database.string()});
  service.migrateAndSeed();
  service.migrateAndSeed();
  verifySchemaVersion(database);

  const auto health = service.handle("GET", "/health", "", "");
  require(health.status == 200, "Health endpoint should be public");
  require(Json::parse(health.body).at("schema_version") == 3,
          "Health endpoint should report schema version");
  require(service.handle("GET", "/v1/profile", "", "").status == 401,
          "Protected endpoints should reject missing credentials");

  service.issueBearerToken(credential_file.string(), std::chrono::hours(1));
  bool refused_overwrite = false;
  try {
    service.issueBearerToken(credential_file.string(), std::chrono::hours(1));
  } catch (const std::exception &) {
    refused_overwrite = true;
  }
  require(refused_overwrite, "Token issuance must not overwrite an existing file");
  bool refused_ttl = false;
  try {
    service.issueBearerToken((directory / "invalid-token.txt").string(),
                             std::chrono::seconds::zero());
  } catch (const std::invalid_argument &) {
    refused_ttl = true;
  }
  require(refused_ttl, "Token issuance must reject a non-positive lifetime");
  const auto token = readToken(credential_file);
  require(token.rfind("office_demo_", 0) == 0, "Issued token has an unexpected format");
  require(token.size() > 40U, "Issued token is unexpectedly short");

  const auto database_bytes = readFile(database);
  require(database_bytes.find(token) == std::string::npos,
          "Database must not contain the bearer token in plaintext");
  const auto authorization = "Bearer " + token;
  require(service.handle("GET", "/v1/profile", authorization, "").status == 200,
          "Issued credential should authorize profile access");
  require(service.handle("GET", "/v1/worklog", authorization, "").status == 200,
          "Worklog endpoint should be available");
  require(service.handle("GET", "/v1/absences", authorization, "").status == 200,
          "Absences endpoint should be available");
  require(service.handle("GET", "/v1/activity", authorization, "").status == 200,
          "Activity endpoint should be available");
  require(
    service
        .handle("GET", "/v1/profile", authorization, "", std::numeric_limits<std::int64_t>::max())
        .status == 401,
    "Expired credentials should be rejected");
}

void testRecognitionBoundsAndPersistence(const std::filesystem::path &directory) {
  const auto database = directory / "recognitions.sqlite3";
  const auto credential_file = directory / "recognitions-token.txt";
  std::string authorization;
  {
    DemoService service({database.string()});
    service.issueBearerToken(credential_file.string(), std::chrono::hours(1));
    authorization = "Bearer " + readToken(credential_file);

    const auto malformed = service.handle("POST", "/v1/recognitions", authorization, "not-json");
    require(malformed.status == 400, "Malformed recognition JSON should be rejected");

    const auto oversized_comment = std::string(281U, 'x');
    const auto invalid = service.handle("POST", "/v1/recognitions", authorization,
                                        Json{{"request_id", "request-too-large"},
                                             {"source_event_id", 1},
                                             {"recipient_id", "alex-rivera"},
                                             {"value", 5},
                                             {"comment", oversized_comment}}
                                          .dump());
    require(invalid.status == 422, "Oversized recognition comment should be rejected");

    const auto unknown_event = service.handle(
      "POST", "/v1/recognitions", authorization,
      R"({"request_id":"request-unknown-event","source_event_id":999,"recipient_id":"alex-rivera","value":1})");
    require(unknown_event.status == 422, "Unknown source event should be rejected");

    const auto unknown_recipient = service.handle(
      "POST", "/v1/recognitions", authorization,
      R"({"request_id":"request-unknown-recipient","source_event_id":1,"recipient_id":"missing-profile","value":1})");
    require(unknown_recipient.status == 422, "Unknown recipient should be rejected");

    const auto unknown_value = service.handle(
      "POST", "/v1/recognitions", authorization,
      R"({"request_id":"request-unknown-value","source_event_id":1,"recipient_id":"alex-rivera","value":7})");
    require(unknown_value.status == 422, "Unknown recognition value should be rejected");

    const auto created = service.handle(
      "POST", "/v1/recognitions", authorization,
      R"({"request_id":"request-001","source_event_id":1,"recipient_id":"alex-rivera","value":3,"comment":"Thank you for the careful review."})",
      1781712000);
    require(created.status == 201, "Bounded recognition should be persisted");
    require(Json::parse(created.body).at("created_at") == 1781712000,
            "Recognition should use the authoritative request time");

    const auto duplicate = service.handle(
      "POST", "/v1/recognitions", authorization,
      R"({"request_id":"request-001","source_event_id":1,"recipient_id":"alex-rivera","value":3,"comment":"Thank you for the careful review."})",
      1781712999);
    require(duplicate.status == 200, "An identical request_id should be idempotent");
    require(Json::parse(duplicate.body).at("created_at") == 1781712000,
            "Idempotent replay should return the original recognition");

    const auto conflict = service.handle(
      "POST", "/v1/recognitions", authorization,
      R"({"request_id":"request-001","source_event_id":1,"recipient_id":"alex-rivera","value":1})");
    require(conflict.status == 409, "Conflicting request_id reuse should be rejected");
  }

  {
    DemoService reopened({database.string()});
    const auto listed = reopened.handle("GET", "/v1/recognitions", authorization, "");
    require(listed.status == 200, "Credential should remain valid after restart");
    const auto items = Json::parse(listed.body).at("items");
    require(items.size() == 1U, "Recognition should survive a database restart");
    require(items.at(0).at("recipient_id") == "alex-rivera",
            "Persisted recognition should retain its recipient");
    require(items.at(0).at("request_id") == "request-001",
            "Persisted recognition should retain its idempotency key");

    reopened.reset();
    require(reopened.handle("GET", "/v1/recognitions", authorization, "").status == 401,
            "Reset should revoke existing bearer credentials");
  }
}

} // namespace

int main() {
  try {
    TemporaryDirectory temporary;
    testVersionOneMigration(temporary.path());
    testMigrationSeedAndAuthentication(temporary.path());
    testRecognitionBoundsAndPersistence(temporary.path());
    std::cout << "All demo server tests passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Demo server test failed: " << error.what() << '\n';
    return 1;
  }
}
