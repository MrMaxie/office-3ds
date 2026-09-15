#include "generator.hpp"
#include "lua_product_loader.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
  std::filesystem::path product;
  std::filesystem::path values;
  std::filesystem::path output;
  std::string expected_slug;
};

Arguments parse_arguments(int argc, char **argv) {
  Arguments arguments;
  for (int index = 1; index < argc; ++index) {
    const std::string option = argv[index];
    if ((option == "--product" || option == "--values" || option == "--output" ||
         option == "--expected-slug") &&
        index + 1 < argc) {
      const std::filesystem::path value = argv[++index];
      if (option == "--product") {
        arguments.product = value;
      } else if (option == "--values") {
        arguments.values = value;
      } else if (option == "--expected-slug") {
        arguments.expected_slug = value.string();
      } else {
        arguments.output = value;
      }
    } else {
      throw std::runtime_error("usage: office_3ds_product_codegen --product <absolute path> "
                               "[--values <path>] [--expected-slug <slug>] --output <build path>");
    }
  }
  if (arguments.product.empty() || arguments.output.empty()) {
    throw std::runtime_error("product and output paths are required");
  }
  return arguments;
}

} // namespace

int main(int argc, char **argv) {
  try {
    const auto arguments = parse_arguments(argc, argv);
    const auto product = office3ds::codegen::load_product(arguments.product, arguments.values);
    if (!arguments.expected_slug.empty() && product.slug != arguments.expected_slug) {
      throw std::runtime_error("configured product slug does not match product.lua");
    }
    office3ds::codegen::generate_product(product, arguments.output / product.slug);
    std::cout << "Generated product '" << product.slug << "'.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Product generation failed: " << error.what() << '\n';
    return 1;
  }
}
