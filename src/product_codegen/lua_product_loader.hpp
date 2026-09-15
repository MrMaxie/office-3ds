#pragma once

#include "product_definition.hpp"

#include <filesystem>

namespace office3ds::codegen {

ProductDefinition load_product(const std::filesystem::path &product_file,
                               const std::filesystem::path &values_file);

} // namespace office3ds::codegen
