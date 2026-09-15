#include "generator.hpp"
#include "lua_product_loader.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>

namespace {

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::map<std::string, std::string> generated_files(const std::filesystem::path &directory) {
  std::map<std::string, std::string> result;
  for (const auto &entry : std::filesystem::directory_iterator(directory)) {
    if (entry.is_regular_file()) {
      result.emplace(entry.path().filename().string(), read_file(entry.path()));
    }
  }
  return result;
}

void write_file(const std::filesystem::path &path, const std::string &contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << contents;
}

void expect_rejected(const std::filesystem::path &product_file, std::string_view expected,
                     const std::filesystem::path &values_file = {}) {
  bool rejected = false;
  try {
    (void)office3ds::codegen::load_product(std::filesystem::absolute(product_file), values_file);
  } catch (const std::runtime_error &error) {
    rejected = std::string_view(error.what()).find(expected) != std::string_view::npos;
  }
  assert(rejected);
}

std::string replace_once(std::string source, std::string_view from, std::string_view to) {
  const auto position = source.find(from);
  assert(position != std::string::npos);
  source.replace(position, from.size(), to);
  return source;
}

} // namespace

int main(int argc, char **argv) {
  assert(argc == 3);
  const auto product_file = std::filesystem::absolute(argv[1]);
  const auto values_file = std::filesystem::absolute(argv[2]);
  const auto product = office3ds::codegen::load_product(product_file, values_file);
  assert(product.schema_version == 1);
  assert(product.api_version == 1);
  assert(product.operations.size() == 5);

  const auto temporary = std::filesystem::temp_directory_path() / "office-3ds-codegen-tests";
  std::filesystem::remove_all(temporary);
  const auto first = temporary / "first";
  const auto second = temporary / "second";
  office3ds::codegen::generate_product(product, first);
  office3ds::codegen::generate_product(product, second);
  assert(generated_files(first) == generated_files(second));
  for (const auto &[name, contents] : generated_files(first)) {
    assert(!name.empty());
    assert(contents.find(product_file.parent_path().string()) == std::string::npos);
  }

  const auto invalid = temporary / "invalid";
  write_file(invalid / "host.lua", "return office.product { schema_version = os.time() }");
  expect_rejected(invalid / "host.lua", "product file failed");

  write_file(invalid / "loop.lua", "while true do end");
  expect_rejected(invalid / "loop.lua", "instruction budget");

  write_file(invalid / "schema.lua", "return office.product { schema_version = 2 }");
  expect_rejected(invalid / "schema.lua", "schema version");

  const auto incompatible = temporary / "incompatible-generated-product";
  std::filesystem::copy(product_file.parent_path(), incompatible,
                        std::filesystem::copy_options::recursive);
  const auto original_product = read_file(product_file);
  write_file(
    incompatible / "product.lua",
    replace_once(
      original_product, "summary = office.string(office.field(\"summary\"))",
      "summary = office.coalesce(office.string(office.field(\"summary\")), \"Activity\")"));
  const auto varied = office3ds::codegen::load_product(incompatible / "product.lua", values_file);
  assert(varied.operations.size() == 5);

  write_file(incompatible / "product.lua",
             replace_once(original_product, "response = office.object {",
                          "response = { _kind = \"unsupported\","));
  expect_rejected(incompatible / "product.lua", "unsupported mapping operator", values_file);

  write_file(incompatible / "product.lua",
             replace_once(original_product, "method = \"GET\",",
                          "method = \"GET\",\n        unexpected = true,"));
  expect_rejected(incompatible / "product.lua", "contains unknown field 'unexpected'", values_file);

  std::filesystem::remove_all(temporary);
  return 0;
}
