#include "generator.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace office3ds::codegen {
namespace {

std::string cpp_string(const std::string &value) {
  std::ostringstream output;
  output << '"';
  for (const unsigned char character : value) {
    switch (character) {
    case '\\':
      output << "\\\\";
      break;
    case '"':
      output << "\\\"";
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
        constexpr char digits[] = "0123456789abcdef";
        output << "\\x" << digits[character >> 4] << digits[character & 0x0f] << "\"\"";
      } else {
        output << static_cast<char>(character);
      }
    }
  }
  output << '"';
  return output.str();
}

void write_file(const std::filesystem::path &path, const std::string &contents) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("cannot write generated output: " + path.filename().string());
  }
  output << contents;
}

std::string map_initializer(const std::map<std::string, std::string> &items) {
  std::ostringstream output;
  output << '{';
  bool first = true;
  for (const auto &[key, value] : items) {
    if (!first) {
      output << ", ";
    }
    first = false;
    output << '{' << cpp_string(key) << ", " << cpp_string(value) << '}';
  }
  output << '}';
  return output.str();
}

std::string cmake_string(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value) {
    if (character == '\\' || character == '"' || character == ';' || character == '$') {
      escaped.push_back('\\');
    }
    escaped.push_back(character);
  }
  return escaped;
}

const Operation &operation_named(const ProductDefinition &product, std::string_view name) {
  const auto found =
    std::find_if(product.operations.begin(), product.operations.end(),
                 [name](const Operation &operation) { return operation.name == name; });
  if (found == product.operations.end()) {
    throw std::runtime_error("cannot generate missing operation: " + std::string(name));
  }
  return *found;
}

} // namespace

void generate_product(const ProductDefinition &product, const std::filesystem::path &output_dir) {
  std::filesystem::create_directories(output_dir);

  write_file(output_dir / "product_config.hpp", R"(#pragma once

#include "office3ds/api/product.hpp"

namespace office3ds::generated {

const api::ProductDescriptor &product_descriptor();
const api::ProductPresentation &product_presentation();

} // namespace office3ds::generated
)");

  std::ostringstream config;
  config << "#include \"product_config.hpp\"\n\n"
            "namespace office3ds::generated {\n\n"
            "const api::ProductDescriptor &product_descriptor() {\n"
            "  static const api::ProductDescriptor value{\n"
         << "      " << product.api_version << ", " << cpp_string(product.slug) << ", "
         << cpp_string(product.display_name) << ",\n      " << cpp_string(product.description)
         << ", " << cpp_string(product.author) << ", " << cpp_string(product.output_basename)
         << ",\n"
         << "      " << cpp_string(product.title_id) << ", " << cpp_string(product.product_code)
         << ", " << cpp_string(product.api_base_url) << ",\n      "
         << cpp_string(product.token_file_environment) << ", " << cpp_string(product.expiry_policy)
         << "};\n"
            "  return value;\n}\n\n"
            "const api::ProductPresentation &product_presentation() {\n"
            "  static const api::ProductPresentation value{"
         << map_initializer(product.palette) << ", " << map_initializer(product.copy) << ", "
         << map_initializer(product.assets)
         << "};\n"
            "  return value;\n}\n\n"
            "} // namespace office3ds::generated\n";
  write_file(output_dir / "product_config.cpp", config.str());

  std::ostringstream requests;
  requests << "#include <array>\n#include <string_view>\n\n"
              "namespace office3ds::generated {\n\n"
              "struct RequestDescriptor { std::string_view name; std::string_view method; "
              "std::string_view path; std::string_view query; std::string_view headers; "
              "std::string_view body; };\n"
              "const std::array<RequestDescriptor, "
           << product.operations.size() << "> request_descriptors{{\n";
  for (const auto &operation : product.operations) {
    requests << "    {" << cpp_string(operation.name) << ", " << cpp_string(operation.method)
             << ", " << cpp_string(operation.path) << ", " << cpp_string(operation.query) << ", "
             << cpp_string(operation.headers) << ", " << cpp_string(operation.body) << "},\n";
  }
  requests << "}};\n\n} // namespace office3ds::generated\n";
  write_file(output_dir / "product_requests.cpp", requests.str());

  std::ostringstream mapper;
  mapper << "#include <array>\n#include <string_view>\n\n"
            "namespace office3ds::generated {\n\n"
            "struct MappingDescriptor { std::string_view operation; std::string_view mapping; };\n"
            "const std::array<MappingDescriptor, "
         << product.operations.size() << "> response_mappings{{\n";
  for (const auto &operation : product.operations) {
    mapper << "    {" << cpp_string(operation.name) << ", " << cpp_string(operation.response)
           << "},\n";
  }
  mapper << "}};\n\n} // namespace office3ds::generated\n";
  write_file(output_dir / "product_mapper.cpp", mapper.str());

  std::ostringstream factory;
  factory << "#include \"office3ds/api/adapter.hpp\"\n"
             "#include \"office3ds/generated_adapter/generated_adapter.hpp\"\n\n"
             "#include <memory>\n\n"
             "namespace office3ds::generated {\n\n";
  if (product.adapter == "generated") {
    const auto &profile = operation_named(product, "profile");
    const auto &worklog = operation_named(product, "worklog");
    const auto &absences = operation_named(product, "absences");
    const auto &activity = operation_named(product, "activity");
    const auto &recognition = operation_named(product, "recognition");
    const auto descriptor = [&](const Operation &operation) {
      factory << "      {" << cpp_string(operation.method) << ", " << cpp_string(operation.path)
              << ", " << cpp_string(operation.query) << ", " << cpp_string(operation.headers)
              << ", " << cpp_string(operation.body) << ", " << cpp_string(operation.response)
              << '}';
    };
    factory << "std::unique_ptr<api::Adapter> create_product_adapter() {\n"
               "  return generated_adapter::create_declarative_adapter({\n";
    descriptor(profile);
    factory << ",\n";
    descriptor(worklog);
    factory << ",\n";
    descriptor(absences);
    factory << ",\n";
    descriptor(activity);
    factory << ",\n";
    descriptor(recognition);
    factory << "});\n}\n";
  } else {
    factory << "// The product-owned sources provide create_product_adapter().\n";
  }
  factory << "\n} // namespace office3ds::generated\n";
  write_file(output_dir / "product_adapter_factory.cpp", factory.str());

  std::ostringstream assets;
  assets << "# Generated deterministically. Do not edit.\n"
            "set(OFFICE_3DS_GENERATED_PRODUCT_SLUG \""
         << cmake_string(product.slug) << "\")\n"
         << "set(OFFICE_3DS_GENERATED_DISPLAY_NAME \"" << cmake_string(product.display_name)
         << "\")\n"
         << "set(OFFICE_3DS_GENERATED_DESCRIPTION \"" << cmake_string(product.description)
         << "\")\n"
         << "set(OFFICE_3DS_GENERATED_AUTHOR \"" << cmake_string(product.author) << "\")\n"
         << "set(OFFICE_3DS_GENERATED_OUTPUT_BASENAME \"" << cmake_string(product.output_basename)
         << "\")\n"
         << "set(OFFICE_3DS_GENERATED_TITLE_ID \"" << cmake_string(product.title_id) << "\")\n"
         << "set(OFFICE_3DS_GENERATED_PRODUCT_CODE \"" << cmake_string(product.product_code)
         << "\")\n"
         << "set(OFFICE_3DS_GENERATED_TOKEN_FILE_ENVIRONMENT \""
         << cmake_string(product.token_file_environment) << "\")\n"
         << "set(OFFICE_3DS_GENERATED_CREDENTIAL_EXPIRY_POLICY \""
         << cmake_string(product.expiry_policy) << "\")\n"
         << "set(OFFICE_3DS_GENERATED_ADAPTER \"" << cmake_string(product.adapter) << "\")\n"
         << "set(OFFICE_3DS_GENERATED_ASSETS\n";
  for (const auto &[name, path] : product.assets) {
    assets << "  \"" << name << '=' << path << "\"\n";
  }
  assets << ")\nset(OFFICE_3DS_GENERATED_ADAPTER_SOURCES\n";
  for (const auto &source : product.adapter_sources) {
    assets << "  \"" << source.generic_string() << "\"\n";
  }
  assets << ")\n";
  write_file(output_dir / "product_assets.cmake", assets.str());
}

} // namespace office3ds::codegen
