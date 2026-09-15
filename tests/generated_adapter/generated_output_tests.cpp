#include "generator.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

office3ds::codegen::Operation operation(std::string name, std::string method, std::string path) {
  return {std::move(name), std::move(method), std::move(path), {}, {}, {}, {}, {}};
}

} // namespace

int main(int argc, char **argv) {
  office3ds::codegen::ProductDefinition product;
  product.schema_version = 1;
  product.api_version = 1;
  product.slug = "office-demo";
  product.display_name = "Office Demo";
  product.description = "Reference product ${RATE}";
  product.author = std::string("MrMaxie\x01", 8) + "a";
  product.output_basename = "office-demo";
  product.title_id = "0x000400000FF40A00";
  product.product_code = "CTR-P-O3DE";
  product.api_base_url = "http://127.0.0.1:8080";
  product.token_file_environment = "OFFICE_3DS_DEMO_TOKEN_FILE";
  product.expiry_policy = "token_expiry";
  product.adapter = "generated";
  product.operations = {
    operation("profile", "GET", "/v1/profile"), operation("worklog", "GET", "/v1/worklog"),
    operation("absences", "GET", "/v1/absences"), operation("activity", "GET", "/v1/activity"),
    operation("recognition", "POST", "/v1/recognitions")};

  assert(argc == 1 || argc == 2);
  const auto output = argc == 2 ? std::filesystem::path(argv[1])
                                : std::filesystem::temp_directory_path() /
                                    "office-3ds-generated-adapter-output-tests";
  std::filesystem::remove_all(output);
  office3ds::codegen::generate_product(product, output);

  const auto factory = read_file(output / "product_adapter_factory.cpp");
  const auto config = read_file(output / "product_config.cpp");
  const auto assets = read_file(output / "product_assets.cmake");
  assert(config.find("token_expiry") != std::string::npos);
  assert(config.find("MrMaxie\\x01\"\"a") != std::string::npos);
  assert(assets.find("OFFICE_3DS_GENERATED_DISPLAY_NAME \"Office Demo\"") != std::string::npos);
  assert(assets.find("Reference product \\${RATE}") != std::string::npos);
  assert(assets.find("OFFICE_3DS_GENERATED_TITLE_ID \"0x000400000FF40A00\"") != std::string::npos);
  assert(assets.find("OFFICE_3DS_GENERATED_CREDENTIAL_EXPIRY_POLICY \"token_expiry\"") !=
         std::string::npos);
  assert(factory.find("generated_adapter::create_declarative_adapter") != std::string::npos);
  assert(factory.find("{\"GET\", \"/v1/profile\",") != std::string::npos);
  assert(factory.find("{\"POST\", \"/v1/recognitions\",") != std::string::npos);
  assert(factory.find("requires a platform response codec") == std::string::npos);
  assert(factory.find("throw") == std::string::npos);

  if (argc == 1) {
    std::filesystem::remove_all(output);
  }
  return 0;
}
