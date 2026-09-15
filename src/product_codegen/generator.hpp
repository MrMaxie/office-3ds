#pragma once

#include "product_definition.hpp"

#include <filesystem>

namespace office3ds::codegen {

void generate_product(const ProductDefinition &product, const std::filesystem::path &output_dir);

} // namespace office3ds::codegen
