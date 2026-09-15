#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <citro2d.h>
#include <citro3d.h>

namespace office3ds::render_3d {

class PreloginScene {
public:
  bool initialize();
  void shutdown();

  void render(C3D_RenderTarget *target, float stereo_strength, bool right_eye);

private:
  bool loadShader();
  bool loadMeshes();
  bool loadProductMark();
  void configureModelAttributes();
  void configureTexturedAttributes();

  std::vector<std::uint32_t> shader_binary_;
  DVLB_s *shader_dvlb_{};
  shaderProgram_s shader_program_{};
  int projection_uniform_ = -1;
  int model_view_uniform_ = -1;

  void *office_vertices_{};
  void *office_indices_{};
  void *mark_vertices_{};
  void *mark_indices_{};
  void *ground_vertices_{};
  void *ground_indices_{};
  void *sky_vertices_{};
  void *sky_indices_{};
  std::size_t ground_index_count_{};
  std::size_t mark_index_count_{};
  float mark_minimum_y_{};

  C2D_SpriteSheet ground_tiles_{};
  C2D_SpriteSheet skybox_{};
};

} // namespace office3ds::render_3d
