#include "office3ds/render_3d/worklog_scene.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include <3ds.h>

namespace office3ds::render_3d {
namespace {

constexpr auto kRecentDayCount = std::size_t{5};
constexpr auto kMaximumBarHeight = 3.15F;
constexpr auto kMinimumBarHeight = 0.42F;
constexpr auto kMaximumEyeDistance = 0.30F;

struct Color {
  std::uint8_t red;
  std::uint8_t green;
  std::uint8_t blue;
  std::uint8_t alpha;
};

struct BarVertex {
  float position[3];
  float normal[3];
  std::uint8_t color[4];
};

using CubeVertices = std::array<BarVertex, 24>;

constexpr std::array<std::uint16_t, 36> kCubeIndices{
  0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
  12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
};

BarVertex vertex(float x, float y, float z, float normal_x, float normal_y, float normal_z,
                 Color color) {
  return {
    {x, y, z}, {normal_x, normal_y, normal_z}, {color.red, color.green, color.blue, color.alpha}};
}

CubeVertices cubeVertices(Color front, Color side, Color top) {
  const auto back = Color{static_cast<std::uint8_t>(side.red * 3U / 4U),
                          static_cast<std::uint8_t>(side.green * 3U / 4U),
                          static_cast<std::uint8_t>(side.blue * 3U / 4U), side.alpha};
  return {
    vertex(-0.5F, 0.0F, 0.5F, 0.0F, 0.0F, 1.0F, front),
    vertex(0.5F, 0.0F, 0.5F, 0.0F, 0.0F, 1.0F, front),
    vertex(0.5F, 1.0F, 0.5F, 0.0F, 0.0F, 1.0F, front),
    vertex(-0.5F, 1.0F, 0.5F, 0.0F, 0.0F, 1.0F, front),
    vertex(0.5F, 0.0F, -0.5F, 0.0F, 0.0F, -1.0F, back),
    vertex(-0.5F, 0.0F, -0.5F, 0.0F, 0.0F, -1.0F, back),
    vertex(-0.5F, 1.0F, -0.5F, 0.0F, 0.0F, -1.0F, back),
    vertex(0.5F, 1.0F, -0.5F, 0.0F, 0.0F, -1.0F, back),
    vertex(0.5F, 0.0F, 0.5F, 1.0F, 0.0F, 0.0F, side),
    vertex(0.5F, 0.0F, -0.5F, 1.0F, 0.0F, 0.0F, side),
    vertex(0.5F, 1.0F, -0.5F, 1.0F, 0.0F, 0.0F, side),
    vertex(0.5F, 1.0F, 0.5F, 1.0F, 0.0F, 0.0F, side),
    vertex(-0.5F, 0.0F, -0.5F, -1.0F, 0.0F, 0.0F, side),
    vertex(-0.5F, 0.0F, 0.5F, -1.0F, 0.0F, 0.0F, side),
    vertex(-0.5F, 1.0F, 0.5F, -1.0F, 0.0F, 0.0F, side),
    vertex(-0.5F, 1.0F, -0.5F, -1.0F, 0.0F, 0.0F, side),
    vertex(-0.5F, 1.0F, 0.5F, 0.0F, 1.0F, 0.0F, top),
    vertex(0.5F, 1.0F, 0.5F, 0.0F, 1.0F, 0.0F, top),
    vertex(0.5F, 1.0F, -0.5F, 0.0F, 1.0F, 0.0F, top),
    vertex(-0.5F, 1.0F, -0.5F, 0.0F, 1.0F, 0.0F, top),
    vertex(-0.5F, 0.0F, -0.5F, 0.0F, -1.0F, 0.0F, back),
    vertex(0.5F, 0.0F, -0.5F, 0.0F, -1.0F, 0.0F, back),
    vertex(0.5F, 0.0F, 0.5F, 0.0F, -1.0F, 0.0F, back),
    vertex(-0.5F, 0.0F, 0.5F, 0.0F, -1.0F, 0.0F, back),
  };
}

void *copyToLinearMemory(const void *source, std::size_t size) {
  auto *destination = linearAlloc(size);
  if (destination != nullptr) {
    std::memcpy(destination, source, size);
  }
  return destination;
}

void configureColorEnvironment() {
  auto *environment = C3D_GetTexEnv(0);
  C3D_TexEnvInit(environment);
  C3D_TexEnvSrc(environment, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
  C3D_TexEnvFunc(environment, C3D_Both, GPU_REPLACE);
  for (auto stage = 1; stage < 6; ++stage) {
    C3D_TexEnvInit(C3D_GetTexEnv(stage));
  }
}

} // namespace

bool WorklogScene::initialize() {
  AttrInfo_Init(&attribute_info_);
  AttrInfo_AddLoader(&attribute_info_, 0, GPU_FLOAT, 3);
  AttrInfo_AddLoader(&attribute_info_, 1, GPU_FLOAT, 3);
  AttrInfo_AddLoader(&attribute_info_, 2, GPU_UNSIGNED_BYTE, 4);
  AttrInfo_AddFixed(&attribute_info_, 3);
  if (!loadShader() || !loadMeshes()) {
    shutdown();
    return false;
  }
  return true;
}

bool WorklogScene::loadShader() {
  auto *file = std::fopen("romfs:/prelogin_scene.shbin", "rb");
  if (file == nullptr || std::fseek(file, 0, SEEK_END) != 0) {
    if (file != nullptr) {
      std::fclose(file);
    }
    return false;
  }
  const auto byte_count = std::ftell(file);
  if (byte_count <= 0 || std::fseek(file, 0, SEEK_SET) != 0) {
    std::fclose(file);
    return false;
  }
  shader_binary_.assign((static_cast<std::size_t>(byte_count) + 3U) / 4U, 0U);
  const auto read =
    std::fread(shader_binary_.data(), 1, static_cast<std::size_t>(byte_count), file);
  std::fclose(file);
  if (read != static_cast<std::size_t>(byte_count)) {
    return false;
  }
  shader_dvlb_ = DVLB_ParseFile(shader_binary_.data(), static_cast<std::uint32_t>(byte_count));
  if (shader_dvlb_ == nullptr) {
    return false;
  }
  shaderProgramInit(&shader_program_);
  shaderProgramSetVsh(&shader_program_, &shader_dvlb_->DVLE[0]);
  projection_uniform_ =
    shaderInstanceGetUniformLocation(shader_program_.vertexShader, "projection");
  model_view_uniform_ = shaderInstanceGetUniformLocation(shader_program_.vertexShader, "modelView");
  return projection_uniform_ >= 0 && model_view_uniform_ >= 0;
}

bool WorklogScene::loadMeshes() {
  constexpr Color green{69, 185, 120, 255};
  constexpr Color green_dark{44, 142, 93, 255};
  constexpr Color green_light{120, 217, 164, 255};
  constexpr Color orange{255, 109, 42, 255};
  constexpr Color orange_dark{198, 75, 24, 255};
  constexpr Color orange_light{255, 146, 95, 255};
  const auto normal = cubeVertices(green, green_dark, green_light);
  const auto selected = cubeVertices(orange, orange_dark, orange_light);
  normal_vertices_ = copyToLinearMemory(normal.data(), sizeof(normal));
  selected_vertices_ = copyToLinearMemory(selected.data(), sizeof(selected));
  indices_ = copyToLinearMemory(kCubeIndices.data(), sizeof(kCubeIndices));
  return normal_vertices_ != nullptr && selected_vertices_ != nullptr && indices_ != nullptr;
}

void WorklogScene::shutdown() {
  const std::array<void **, 3> buffers{&normal_vertices_, &selected_vertices_, &indices_};
  for (auto **buffer : buffers) {
    if (*buffer != nullptr) {
      linearFree(*buffer);
      *buffer = nullptr;
    }
  }
  if (shader_dvlb_ != nullptr) {
    shaderProgramFree(&shader_program_);
    DVLB_Free(shader_dvlb_);
    shader_dvlb_ = nullptr;
  }
  shader_binary_.clear();
}

void WorklogScene::render(C3D_RenderTarget *target, const std::vector<api::WorklogDay> &worklog,
                          std::size_t selected_index, float stereo_strength, bool right_eye) {
  if (worklog.empty()) {
    return;
  }
  const auto visible_count = std::min(kRecentDayCount, worklog.size());
  const auto first_visible = worklog.size() - visible_count;
  auto scale_minutes = std::int32_t{480};
  for (auto index = first_visible; index < worklog.size(); ++index) {
    scale_minutes = std::max(scale_minutes, std::max(worklog[index].minutes, std::int32_t{0}));
    scale_minutes =
      std::max(scale_minutes, std::max(worklog[index].expected_minutes, std::int32_t{0}));
  }

  C3D_FrameSplit(0);
  C3D_RenderTargetClear(target, C3D_CLEAR_DEPTH, 0, 0);
  C3D_FrameDrawOn(target);
  C3D_BindProgram(&shader_program_);
  C3D_CullFace(GPU_CULL_NONE);
  C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
  C3D_SetAttrInfo(&attribute_info_);
  C3D_FixedAttribSet(3, 0.0F, 0.0F, 0.0F, 0.0F);
  configureColorEnvironment();

  const auto eye_distance = std::clamp(stereo_strength, 0.0F, 1.0F) * kMaximumEyeDistance;
  const auto iod = (right_eye ? 1.0F : -1.0F) * eye_distance;
  C3D_Mtx projection;
  Mtx_PerspStereoTilt(&projection, C3D_AngleFromDegrees(46.0F), C3D_AspectRatioTop, 0.1F, 100.0F,
                      iod, 8.5F, false);
  C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, projection_uniform_, &projection);

  const auto camera_position = FVec4_New(-1.35F, 3.7F, -8.5F, 1.0F);
  const auto camera_target = FVec4_New(-1.35F, 1.2F, 0.0F, 1.0F);
  const auto camera_up = FVec4_New(0.0F, 1.0F, 0.0F, 0.0F);
  C3D_Mtx view;
  Mtx_LookAt(&view, camera_position, camera_target, camera_up, false);

  C3D_BufInfo buffer_info;
  C3D_Mtx model_view;

  for (auto visible_index = std::size_t{0}; visible_index < visible_count; ++visible_index) {
    const auto day_index = first_visible + visible_index;
    const auto &day = worklog[day_index];
    const auto height =
      std::max(kMinimumBarHeight, static_cast<float>(day.minutes) /
                                    static_cast<float>(scale_minutes) * kMaximumBarHeight);
    model_view = view;
    Mtx_Translate(&model_view, 3.75F - static_cast<float>(visible_index) * 1.35F, 0.0F,
                  day_index == selected_index ? -0.34F : 0.0F, true);
    Mtx_Scale(&model_view, 0.56F, height, 0.72F);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, model_view_uniform_, &model_view);
    BufInfo_Init(&buffer_info);
    BufInfo_Add(&buffer_info, day_index == selected_index ? selected_vertices_ : normal_vertices_,
                sizeof(BarVertex), 3, 0x210);
    C3D_SetBufInfo(&buffer_info);
    C3D_DrawElements(GPU_TRIANGLES, static_cast<int>(kCubeIndices.size()), C3D_UNSIGNED_SHORT,
                     indices_);
  }
}

} // namespace office3ds::render_3d
