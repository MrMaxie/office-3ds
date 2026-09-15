#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <citro3d.h>

#include "office3ds/api/models.hpp"

namespace office3ds::render_3d {

class WorklogScene {
public:
  bool initialize();
  void shutdown();

  void render(C3D_RenderTarget *target, const std::vector<api::WorklogDay> &worklog,
              std::size_t selected_index, float stereo_strength, bool right_eye);

private:
  bool loadShader();
  bool loadMeshes();

  std::vector<std::uint32_t> shader_binary_;
  DVLB_s *shader_dvlb_{};
  shaderProgram_s shader_program_{};
  int projection_uniform_ = -1;
  int model_view_uniform_ = -1;
  C3D_AttrInfo attribute_info_{};

  void *normal_vertices_{};
  void *selected_vertices_{};
  void *indices_{};
};

} // namespace office3ds::render_3d
