#include "lua_product_loader.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace office3ds::codegen {
namespace {

constexpr std::size_t kMaxProductBytes = 256 * 1024;
constexpr std::size_t kMaxValuesBytes = 64 * 1024;
constexpr std::size_t kMaxLuaBytes = 8 * 1024 * 1024;
constexpr int kHookInterval = 1000;
constexpr int kInstructionBudget = 500000;

struct LuaMemory {
  std::size_t used = 0;
  std::size_t limit = kMaxLuaBytes;
};

void *bounded_alloc(void *context, void *pointer, std::size_t old_size, std::size_t new_size) {
  auto &memory = *static_cast<LuaMemory *>(context);
  if (new_size == 0) {
    if (pointer != nullptr) {
      memory.used -= std::min(memory.used, old_size);
      std::free(pointer);
    }
    return nullptr;
  }
  const std::size_t accounted_old = pointer == nullptr ? 0 : old_size;
  if (new_size > memory.limit - (memory.used - std::min(memory.used, accounted_old))) {
    return nullptr;
  }
  void *result = std::realloc(pointer, new_size);
  if (result != nullptr) {
    memory.used = memory.used - std::min(memory.used, accounted_old) + new_size;
  }
  return result;
}

void instruction_hook(lua_State *state, lua_Debug *) {
  lua_getfield(state, LUA_REGISTRYINDEX, "office.instruction_budget");
  const auto remaining = lua_tointeger(state, -1) - kHookInterval;
  lua_pop(state, 1);
  if (remaining <= 0) {
    luaL_error(state, "product script exceeded the instruction budget");
  }
  lua_pushinteger(state, remaining);
  lua_setfield(state, LUA_REGISTRYINDEX, "office.instruction_budget");
}

class LuaState {
public:
  LuaState() : state_(lua_newstate(bounded_alloc, &memory_)) {
    if (state_ == nullptr) {
      throw std::runtime_error("cannot create bounded Lua state");
    }
    lua_pushinteger(state_, kInstructionBudget);
    lua_setfield(state_, LUA_REGISTRYINDEX, "office.instruction_budget");
    lua_sethook(state_, instruction_hook, LUA_MASKCOUNT, kHookInterval);
  }

  ~LuaState() { lua_close(state_); }
  LuaState(const LuaState &) = delete;
  LuaState &operator=(const LuaState &) = delete;
  [[nodiscard]] lua_State *get() const { return state_; }

private:
  LuaMemory memory_;
  lua_State *state_;
};

std::string read_bounded_file(const std::filesystem::path &path, std::size_t limit,
                              std::string_view label) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size > limit) {
    throw std::runtime_error(std::string(label) + " is missing, unreadable, or too large");
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error(std::string(label) + " is missing, unreadable, or too large");
  }
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void run_chunk(lua_State *state, const std::string &source, std::string_view chunk_name,
               bool redact_error) {
  if (luaL_loadbufferx(state, source.data(), source.size(), std::string(chunk_name).c_str(), "t") !=
        LUA_OK ||
      lua_pcall(state, 0, 1, 0) != LUA_OK) {
    std::string detail =
      lua_tostring(state, -1) != nullptr ? lua_tostring(state, -1) : "unknown error";
    lua_pop(state, 1);
    if (redact_error) {
      throw std::runtime_error(std::string(chunk_name) + " is invalid; contents were redacted");
    }
    throw std::runtime_error(std::string(chunk_name) + " failed: " + detail);
  }
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    throw std::runtime_error(std::string(chunk_name) + " must return a table");
  }
}

std::map<std::string, std::string> load_values(const std::filesystem::path &values_file) {
  if (values_file.empty()) {
    return {};
  }
  LuaState lua;
  const auto source = read_bounded_file(values_file, kMaxValuesBytes, "values file");
  run_chunk(lua.get(), source, "values file", true);

  std::map<std::string, std::string> values;
  lua_pushnil(lua.get());
  while (lua_next(lua.get(), -2) != 0) {
    if (lua_type(lua.get(), -2) != LUA_TSTRING || lua_type(lua.get(), -1) != LUA_TSTRING) {
      throw std::runtime_error("values file must contain only string keys and string values");
    }
    values.emplace(lua_tostring(lua.get(), -2), lua_tostring(lua.get(), -1));
    lua_pop(lua.get(), 1);
  }
  return values;
}

int tagged_table(lua_State *state) {
  const char *kind = lua_tostring(state, lua_upvalueindex(1));
  const int arguments = lua_gettop(state);
  const std::string_view kind_view(kind);
  const bool tags_argument =
    kind_view == "product" || kind_view == "request" || kind_view == "object";
  if (tags_argument && arguments == 1 && lua_istable(state, 1)) {
    lua_pushstring(state, kind);
    lua_setfield(state, 1, "_kind");
    lua_settop(state, 1);
    return 1;
  }
  lua_createtable(state, arguments, 1);
  for (int index = 1; index <= arguments; ++index) {
    lua_pushvalue(state, index);
    lua_rawseti(state, -2, index);
  }
  lua_pushstring(state, kind);
  lua_setfield(state, -2, "_kind");
  return 1;
}

void add_dsl_function(lua_State *state, const char *name, const char *kind) {
  lua_pushstring(state, kind);
  lua_pushcclosure(state, tagged_table, 1);
  lua_setfield(state, -2, name);
}

void install_dsl(lua_State *state) {
  lua_newtable(state);
  add_dsl_function(state, "product", "product");
  add_dsl_function(state, "required_value", "required_value");
  add_dsl_function(state, "request", "request");
  add_dsl_function(state, "field", "field");
  add_dsl_function(state, "optional", "optional");
  add_dsl_function(state, "default", "default");
  add_dsl_function(state, "coalesce", "coalesce");
  add_dsl_function(state, "object", "object");
  add_dsl_function(state, "list", "list");
  add_dsl_function(state, "string", "string");
  add_dsl_function(state, "number", "number");
  add_dsl_function(state, "date", "date");
  add_dsl_function(state, "context", "context");
  add_dsl_function(state, "url_encode", "url_encode");
  lua_setglobal(state, "office");
}

int absolute(lua_State *state, int index) { return lua_absindex(state, index); }

void require_table_field(lua_State *state, int table, const char *field) {
  lua_getfield(state, absolute(state, table), field);
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    throw std::runtime_error(std::string("field '") + field + "' must be a table");
  }
}

std::string required_string(lua_State *state, int table, const char *field) {
  lua_getfield(state, absolute(state, table), field);
  if (lua_type(state, -1) != LUA_TSTRING) {
    lua_pop(state, 1);
    throw std::runtime_error(std::string("field '") + field + "' must be a string");
  }
  std::string value = lua_tostring(state, -1);
  lua_pop(state, 1);
  if (value.empty()) {
    throw std::runtime_error(std::string("field '") + field + "' must not be empty");
  }
  return value;
}

unsigned required_unsigned(lua_State *state, int table, const char *field) {
  lua_getfield(state, absolute(state, table), field);
  int is_number = 0;
  const auto value = lua_tointegerx(state, -1, &is_number);
  lua_pop(state, 1);
  if (is_number == 0 || value < 0 ||
      static_cast<unsigned long long>(value) > std::numeric_limits<unsigned>::max()) {
    throw std::runtime_error(std::string("field '") + field + "' must be an unsigned integer");
  }
  return static_cast<unsigned>(value);
}

void ensure_known_keys(lua_State *state, int table, const std::set<std::string> &known,
                       std::string_view context) {
  table = absolute(state, table);
  lua_pushnil(state);
  while (lua_next(state, table) != 0) {
    if (lua_type(state, -2) == LUA_TSTRING) {
      const std::string key = lua_tostring(state, -2);
      if (known.count(key) == 0) {
        lua_pop(state, 2);
        throw std::runtime_error(std::string(context) + " contains unknown field '" + key + "'");
      }
    }
    lua_pop(state, 1);
  }
}

std::map<std::string, std::string> string_map(lua_State *state, int table, const char *field) {
  lua_getfield(state, absolute(state, table), field);
  if (lua_isnil(state, -1)) {
    lua_pop(state, 1);
    return {};
  }
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    throw std::runtime_error(std::string("field '") + field + "' must be a table");
  }
  std::map<std::string, std::string> result;
  lua_pushnil(state);
  while (lua_next(state, -2) != 0) {
    if (lua_type(state, -2) != LUA_TSTRING || lua_type(state, -1) != LUA_TSTRING) {
      throw std::runtime_error(std::string("field '") + field + "' must map strings to strings");
    }
    result.emplace(lua_tostring(state, -2), lua_tostring(state, -1));
    lua_pop(state, 1);
  }
  lua_pop(state, 1);
  return result;
}

std::string resolve_value(lua_State *state, int table, const char *field,
                          const std::map<std::string, std::string> &values) {
  lua_getfield(state, absolute(state, table), field);
  if (lua_type(state, -1) == LUA_TSTRING) {
    std::string value = lua_tostring(state, -1);
    lua_pop(state, 1);
    return value;
  }
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    throw std::runtime_error(std::string("field '") + field + "' requires a local value");
  }
  lua_getfield(state, -1, "_kind");
  const bool required = lua_type(state, -1) == LUA_TSTRING &&
                        std::string_view(lua_tostring(state, -1)) == "required_value";
  lua_pop(state, 1);
  lua_rawgeti(state, -1, 1);
  const std::string key = lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : "";
  lua_pop(state, 2);
  if (!required || key.empty()) {
    throw std::runtime_error(std::string("field '") + field + "' has an invalid value reference");
  }
  const auto found = values.find(key);
  if (found == values.end() || found->second.empty()) {
    throw std::runtime_error("required local value '" + key + "' is missing");
  }
  return found->second;
}

std::string json_string(std::string_view value) {
  std::ostringstream output;
  output << '"';
  constexpr char digits[] = "0123456789abcdef";
  for (const unsigned char character : value) {
    switch (character) {
    case '\\':
      output << "\\\\";
      break;
    case '"':
      output << "\\\"";
      break;
    case '\b':
      output << "\\b";
      break;
    case '\f':
      output << "\\f";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      if (character < 0x20) {
        output << "\\u00" << digits[character >> 4] << digits[character & 0x0f];
      } else {
        output << static_cast<char>(character);
      }
    }
  }
  output << '"';
  return output.str();
}

std::string canonical_value(lua_State *state, int index, int depth = 0) {
  if (depth > 24) {
    throw std::runtime_error("mapping exceeds the maximum nesting depth");
  }
  index = absolute(state, index);
  switch (lua_type(state, index)) {
  case LUA_TNIL:
    return "null";
  case LUA_TBOOLEAN:
    return lua_toboolean(state, index) != 0 ? "true" : "false";
  case LUA_TNUMBER: {
    std::ostringstream output;
    if (lua_isinteger(state, index)) {
      output << lua_tointeger(state, index);
    } else {
      output.precision(17);
      output << lua_tonumber(state, index);
    }
    return output.str();
  }
  case LUA_TSTRING:
    return json_string(lua_tostring(state, index));
  case LUA_TTABLE: {
    std::vector<std::pair<std::string, std::string>> entries;
    lua_pushnil(state);
    while (lua_next(state, index) != 0) {
      std::string key;
      if (lua_type(state, -2) == LUA_TSTRING) {
        key = lua_tostring(state, -2);
      } else if (lua_isinteger(state, -2)) {
        key = std::to_string(lua_tointeger(state, -2));
      } else {
        throw std::runtime_error("mapping table keys must be strings or integers");
      }
      entries.emplace_back(std::move(key), canonical_value(state, -1, depth + 1));
      lua_pop(state, 1);
    }
    std::sort(entries.begin(), entries.end());
    std::ostringstream output;
    output << '{';
    for (std::size_t i = 0; i < entries.size(); ++i) {
      if (i != 0) {
        output << ',';
      }
      output << json_string(entries[i].first) << ':' << entries[i].second;
    }
    output << '}';
    return output.str();
  }
  default:
    throw std::runtime_error("product schema contains an unsupported Lua value");
  }
}

void validate_mapping(lua_State *state, int index, int depth = 0) {
  if (depth > 24) {
    throw std::runtime_error("mapping exceeds the maximum nesting depth");
  }
  index = absolute(state, index);
  if (!lua_istable(state, index)) {
    return;
  }
  lua_getfield(state, index, "_kind");
  if (lua_type(state, -1) == LUA_TSTRING) {
    static const std::set<std::string> kinds = {"request",  "field",  "optional", "default",
                                                "coalesce", "object", "list",     "string",
                                                "number",   "date",   "context",  "url_encode"};
    const std::string kind = lua_tostring(state, -1);
    if (kinds.count(kind) == 0) {
      lua_pop(state, 1);
      throw std::runtime_error("unsupported mapping operator '" + kind + "'");
    }
  }
  lua_pop(state, 1);
  lua_pushnil(state);
  while (lua_next(state, index) != 0) {
    validate_mapping(state, -1, depth + 1);
    lua_pop(state, 1);
  }
}

std::filesystem::path checked_relative_path(const std::filesystem::path &root,
                                            const std::string &relative, bool must_exist) {
  const std::filesystem::path input(relative);
  if (input.empty() || input.is_absolute()) {
    throw std::runtime_error("product path must be relative");
  }
  const auto normalized = input.lexically_normal();
  if (normalized.empty() || *normalized.begin() == "..") {
    throw std::runtime_error("product path escapes the product directory");
  }
  std::error_code error;
  const auto canonical_root = std::filesystem::weakly_canonical(root, error);
  if (error) {
    throw std::runtime_error("cannot resolve product directory");
  }
  const auto resolved = std::filesystem::weakly_canonical(root / normalized, error);
  if (error) {
    throw std::runtime_error("cannot resolve declared product path");
  }
  const auto relative_to_root = std::filesystem::relative(resolved, canonical_root, error);
  if (error || relative_to_root.empty() || *relative_to_root.begin() == "..") {
    throw std::runtime_error("product path escapes the product directory");
  }
  if (must_exist && !std::filesystem::is_regular_file(resolved)) {
    throw std::runtime_error("declared product file does not exist: " + relative);
  }
  return normalized;
}

bool valid_slug(std::string_view value) {
  return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return std::islower(character) != 0 || std::isdigit(character) != 0 || character == '-';
  });
}

bool valid_hex(std::string_view value, std::size_t size) {
  if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0) {
    value.remove_prefix(2);
  }
  return value.size() == size &&
         std::all_of(value.begin(), value.end(),
                     [](unsigned char character) { return std::isxdigit(character) != 0; });
}

void validate_generated_adapter_contract(const ProductDefinition &product) {
  const std::map<std::string, std::string> required_methods = {
    {"profile", "GET"},  {"worklog", "GET"},      {"absences", "GET"},
    {"activity", "GET"}, {"recognition", "POST"},
  };
  for (const auto &[name, method] : required_methods) {
    const auto found =
      std::find_if(product.operations.begin(), product.operations.end(),
                   [&](const Operation &operation) { return operation.name == name; });
    if (found == product.operations.end() || found->method != method || found->response == "null" ||
        (name == "recognition" && found->body == "null")) {
      throw std::runtime_error("generated adapter mode requires the five schema-v1 operations");
    }
  }
}

ProductDefinition parse_product(lua_State *state, const std::filesystem::path &root,
                                const std::map<std::string, std::string> &values) {
  ProductDefinition product;
  ensure_known_keys(
    state, -1, {"_kind", "schema_version", "identity", "presentation", "backend", "extensions"},
    "product");
  product.schema_version = required_unsigned(state, -1, "schema_version");
  if (product.schema_version != 1) {
    throw std::runtime_error("product schema version must be 1");
  }

  require_table_field(state, -1, "identity");
  ensure_known_keys(state, -1,
                    {"slug", "display_name", "description", "author", "output_basename", "title_id",
                     "product_code"},
                    "identity");
  product.slug = required_string(state, -1, "slug");
  product.display_name = required_string(state, -1, "display_name");
  product.description = required_string(state, -1, "description");
  product.author = required_string(state, -1, "author");
  product.output_basename = required_string(state, -1, "output_basename");
  product.title_id = required_string(state, -1, "title_id");
  product.product_code = required_string(state, -1, "product_code");
  lua_pop(state, 1);
  if (!valid_slug(product.slug) || !valid_hex(product.title_id, 16) ||
      product.product_code.rfind("CTR-P-", 0) != 0 || product.product_code.size() != 10) {
    throw std::runtime_error("identity contains an invalid slug or Nintendo 3DS identifier");
  }

  require_table_field(state, -1, "presentation");
  ensure_known_keys(state, -1, {"palette", "copy", "assets"}, "presentation");
  product.palette = string_map(state, -1, "palette");
  product.copy = string_map(state, -1, "copy");
  product.assets = string_map(state, -1, "assets");
  for (auto &[name, path] : product.assets) {
    path = checked_relative_path(root, path, true).generic_string();
  }
  lua_pop(state, 1);

  require_table_field(state, -1, "backend");
  ensure_known_keys(state, -1, {"api_base_url", "auth", "operations"}, "backend");
  product.api_base_url = resolve_value(state, -1, "api_base_url", values);
  require_table_field(state, -1, "auth");
  ensure_known_keys(state, -1, {"kind", "token_file_env", "expiry"}, "backend.auth");
  if (required_string(state, -1, "kind") != "bearer_file") {
    throw std::runtime_error("backend.auth.kind must be 'bearer_file'");
  }
  product.token_file_environment = required_string(state, -1, "token_file_env");
  product.expiry_policy = required_string(state, -1, "expiry");
  if (product.expiry_policy != "jwt_exp" && product.expiry_policy != "token_expiry") {
    throw std::runtime_error("backend.auth.expiry must be 'jwt_exp' or 'token_expiry'");
  }
  lua_pop(state, 1);

  require_table_field(state, -1, "operations");
  const int operations = absolute(state, -1);
  static const std::vector<std::string> required_operations = {"profile", "worklog", "absences",
                                                               "activity", "recognition"};
  std::vector<std::string> operation_names;
  lua_pushnil(state);
  while (lua_next(state, operations) != 0) {
    if (lua_type(state, -2) != LUA_TSTRING || !lua_istable(state, -1)) {
      throw std::runtime_error("backend operations must map names to request declarations");
    }
    operation_names.emplace_back(lua_tostring(state, -2));
    lua_pop(state, 1);
  }
  std::sort(operation_names.begin(), operation_names.end());
  for (const auto &required : required_operations) {
    if (!std::binary_search(operation_names.begin(), operation_names.end(), required)) {
      throw std::runtime_error("missing backend operation '" + required + "'");
    }
  }
  for (const auto &name : operation_names) {
    lua_getfield(state, operations, name.c_str());
    ensure_known_keys(
      state, -1, {"_kind", "method", "path", "query", "headers", "body", "response", "depends_on"},
      "backend operation '" + name + "'");
    validate_mapping(state, -1);
    Operation operation;
    operation.name = name;
    operation.method = required_string(state, -1, "method");
    operation.path = required_string(state, -1, "path");
    static const std::set<std::string> methods = {"GET", "POST", "PUT", "PATCH", "DELETE"};
    if (methods.count(operation.method) == 0 || operation.path.front() != '/') {
      throw std::runtime_error("operation '" + name + "' has an invalid method or path");
    }
    for (const auto &[field, destination] :
         std::vector<std::pair<const char *, std::string *>>{{"query", &operation.query},
                                                             {"headers", &operation.headers},
                                                             {"body", &operation.body},
                                                             {"response", &operation.response}}) {
      lua_getfield(state, -1, field);
      *destination = canonical_value(state, -1);
      lua_pop(state, 1);
    }
    lua_getfield(state, -1, "depends_on");
    if (lua_istable(state, -1)) {
      const auto count = lua_rawlen(state, -1);
      for (std::size_t index = 1; index <= count; ++index) {
        lua_rawgeti(state, -1, static_cast<lua_Integer>(index));
        if (lua_type(state, -1) != LUA_TSTRING) {
          throw std::runtime_error("operation dependency must be a string");
        }
        operation.depends_on.emplace_back(lua_tostring(state, -1));
        lua_pop(state, 1);
      }
    }
    lua_pop(state, 1);
    product.operations.push_back(std::move(operation));
    lua_pop(state, 1);
  }
  std::map<std::string, std::vector<std::string>> graph;
  for (const auto &operation : product.operations) {
    for (const auto &dependency : operation.depends_on) {
      if (!std::binary_search(operation_names.begin(), operation_names.end(), dependency)) {
        throw std::runtime_error("operation '" + operation.name + "' has an unknown dependency");
      }
    }
    graph.emplace(operation.name, operation.depends_on);
  }
  std::set<std::string> visiting;
  std::set<std::string> visited;
  std::function<void(const std::string &)> visit = [&](const std::string &name) {
    if (visiting.count(name) != 0) {
      throw std::runtime_error("backend operation dependencies contain a cycle");
    }
    if (visited.count(name) != 0) {
      return;
    }
    visiting.insert(name);
    for (const auto &dependency : graph.at(name)) {
      visit(dependency);
    }
    visiting.erase(name);
    visited.insert(name);
  };
  for (const auto &name : operation_names) {
    visit(name);
  }
  lua_pop(state, 2);

  require_table_field(state, -1, "extensions");
  ensure_known_keys(state, -1, {"product_api_version", "adapter", "sources"}, "extensions");
  product.api_version = required_unsigned(state, -1, "product_api_version");
  if (product.api_version != 1) {
    throw std::runtime_error("product API version must be 1");
  }
  product.adapter = required_string(state, -1, "adapter");
  if (product.adapter != "generated" && product.adapter != "cpp") {
    throw std::runtime_error("extensions.adapter must be 'generated' or 'cpp'");
  }
  lua_getfield(state, -1, "sources");
  if (product.adapter == "cpp") {
    if (!lua_istable(state, -1) || lua_rawlen(state, -1) == 0) {
      throw std::runtime_error("C++ adapter mode requires relative sources");
    }
    for (std::size_t index = 1; index <= lua_rawlen(state, -1); ++index) {
      lua_rawgeti(state, -1, static_cast<lua_Integer>(index));
      if (lua_type(state, -1) != LUA_TSTRING) {
        throw std::runtime_error("adapter source paths must be strings");
      }
      product.adapter_sources.push_back(checked_relative_path(root, lua_tostring(state, -1), true));
      lua_pop(state, 1);
    }
  } else if (!lua_isnil(state, -1)) {
    throw std::runtime_error("generated adapter mode does not accept custom sources");
  }
  lua_pop(state, 2);
  if (product.adapter == "generated") {
    validate_generated_adapter_contract(product);
  }
  return product;
}

} // namespace

ProductDefinition load_product(const std::filesystem::path &product_file,
                               const std::filesystem::path &values_file) {
  if (!product_file.is_absolute()) {
    throw std::runtime_error("product file path must be absolute");
  }
  const auto root = product_file.parent_path();
  const auto source = read_bounded_file(product_file, kMaxProductBytes, "product file");
  const auto values = load_values(values_file);

  LuaState lua;
  install_dsl(lua.get());
  run_chunk(lua.get(), source, "product file", false);
  return parse_product(lua.get(), root, values);
}

} // namespace office3ds::codegen
