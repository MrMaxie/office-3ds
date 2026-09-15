#pragma once

#include "office3ds/api/adapter.hpp"

#include <memory>
#include <string>

namespace office3ds::generated_adapter {

struct OperationDescriptor {
  std::string method;
  std::string path;
  std::string query;
  std::string headers;
  std::string body;
  std::string response;
};

struct DeclarativeOperations {
  OperationDescriptor profile;
  OperationDescriptor worklog;
  OperationDescriptor absences;
  OperationDescriptor activity;
  OperationDescriptor recognition;
};

[[nodiscard]] std::unique_ptr<api::Adapter>
create_declarative_adapter(DeclarativeOperations operations);

} // namespace office3ds::generated_adapter
