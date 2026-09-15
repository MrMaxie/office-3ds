#pragma once

#include <string_view>

#include <citro2d.h>

namespace office3ds::app {

class AvatarImages {
public:
  virtual ~AvatarImages() = default;
  [[nodiscard]] virtual const C2D_Image *find(std::string_view url) const = 0;
};

} // namespace office3ds::app
