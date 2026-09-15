#include "office3ds/render_3d/prelogin_scene.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include <3ds.h>

#include "office3ds/render_3d/neutral_office_mesh.hpp"

namespace office3ds::render_3d {
namespace {

constexpr auto kClearColor = 0x101010FFU;
constexpr auto kGroundColumns = 21;
constexpr auto kGroundRows = 12;
constexpr auto kGroundHeight = 10.0F;
constexpr auto kGroundWidth =
  kGroundHeight * static_cast<float>(kGroundColumns) / static_cast<float>(kGroundRows);
constexpr auto kTileWorldSize = kGroundHeight / static_cast<float>(kGroundRows);
constexpr auto kModelScale = kTileWorldSize;
constexpr auto kModelDepth = 3.875F;
constexpr auto kActorZ = kGroundHeight / 2.0F - kModelDepth * kModelScale / 2.0F;
constexpr auto kEntranceZ = kActorZ - kModelDepth * kModelScale / 2.0F;
constexpr auto kSourcePixelWorldSize = kModelScale / 16.0F;
constexpr auto kMarkX = -48.0F * kSourcePixelWorldSize;
constexpr auto kMarkDepth = 30.0F * kSourcePixelWorldSize;
constexpr auto kMarkFrontZ = kEntranceZ - 16.0F * kSourcePixelWorldSize;
constexpr auto kMarkZ = kMarkFrontZ + kMarkDepth / 2.0F;
constexpr auto kCameraFov = 1.15F;
constexpr auto kMaximumEyeDistance = 0.5F;
constexpr std::array<char, 8> kProductMeshMagic{'O', '3', 'M', 'S', 'H', '0', '0', '1'};
constexpr std::uint32_t kMaximumProductVertices = 65'535;
constexpr std::uint32_t kMaximumProductIndices = 393'210;

struct SceneVertex {
  float position[3];
  float normal[3];
  std::uint8_t color[4];
  float texture[2];
};

struct TextureBounds {
  float left;
  float top;
  float right;
  float bottom;
};

void *copyToLinearMemory(const void *source, std::size_t size) {
  auto *destination = linearAlloc(size);
  if (destination != nullptr) {
    std::memcpy(destination, source, size);
  }
  return destination;
}

TextureBounds textureBounds(C2D_Image image, float left, float top, float right, float bottom) {
  const auto horizontal = image.subtex->right - image.subtex->left;
  const auto vertical = image.subtex->bottom - image.subtex->top;
  return {
    image.subtex->left + horizontal * left,
    image.subtex->top + vertical * top,
    image.subtex->left + horizontal * right,
    image.subtex->top + vertical * bottom,
  };
}

bool buildGroundMesh(C2D_Image atlas, void *&vertices_output, void *&indices_output,
                     std::size_t &index_count_output) {
  const auto texture = textureBounds(atlas, 0.0F, 0.0F, 1.0F, 1.0F);
  constexpr std::array<std::uint8_t, 4> white{255, 255, 255, 255};
  const std::array<SceneVertex, 4> vertices{
    SceneVertex{{-kGroundWidth / 2.0F, 0.0F, -kGroundHeight / 2.0F},
                {0.0F, 1.0F, 0.0F},
                {white[0], white[1], white[2], white[3]},
                {texture.left, texture.bottom}},
    SceneVertex{{kGroundWidth / 2.0F, 0.0F, -kGroundHeight / 2.0F},
                {0.0F, 1.0F, 0.0F},
                {white[0], white[1], white[2], white[3]},
                {texture.right, texture.bottom}},
    SceneVertex{{kGroundWidth / 2.0F, 0.0F, kGroundHeight / 2.0F},
                {0.0F, 1.0F, 0.0F},
                {white[0], white[1], white[2], white[3]},
                {texture.right, texture.top}},
    SceneVertex{{-kGroundWidth / 2.0F, 0.0F, kGroundHeight / 2.0F},
                {0.0F, 1.0F, 0.0F},
                {white[0], white[1], white[2], white[3]},
                {texture.left, texture.top}},
  };
  constexpr std::array<std::uint16_t, 6> indices{0, 2, 1, 0, 3, 2};
  vertices_output = copyToLinearMemory(vertices.data(), sizeof(vertices));
  indices_output = copyToLinearMemory(indices.data(), sizeof(indices));
  index_count_output = indices.size();
  return vertices_output != nullptr && indices_output != nullptr;
}

bool buildSkyMesh(C2D_Image skybox, void *&vertices_output, void *&indices_output) {
  constexpr auto source_aspect = 510.0F / 260.0F;
  constexpr auto view_aspect = 400.0F / 240.0F;
  constexpr auto horizontal_sample = view_aspect / source_aspect;
  constexpr auto horizontal_margin = (1.0F - horizontal_sample) / 2.0F;
  const auto texture =
    textureBounds(skybox, horizontal_margin, 0.0F, 1.0F - horizontal_margin, 1.0F);
  constexpr std::array<std::uint8_t, 4> white{255, 255, 255, 255};
  const std::array<SceneVertex, 4> vertices{
    SceneVertex{{-200.0F, 0.0F, 0.5F},
                {0.0F, 0.0F, 1.0F},
                {white[0], white[1], white[2], white[3]},
                {texture.left, texture.bottom}},
    SceneVertex{{200.0F, 0.0F, 0.5F},
                {0.0F, 0.0F, 1.0F},
                {white[0], white[1], white[2], white[3]},
                {texture.right, texture.bottom}},
    SceneVertex{{200.0F, 240.0F, 0.5F},
                {0.0F, 0.0F, 1.0F},
                {white[0], white[1], white[2], white[3]},
                {texture.right, texture.top}},
    SceneVertex{{-200.0F, 240.0F, 0.5F},
                {0.0F, 0.0F, 1.0F},
                {white[0], white[1], white[2], white[3]},
                {texture.left, texture.top}},
  };
  constexpr std::array<std::uint16_t, 6> indices{0, 2, 1, 0, 3, 2};
  vertices_output = copyToLinearMemory(vertices.data(), sizeof(vertices));
  indices_output = copyToLinearMemory(indices.data(), sizeof(indices));
  return vertices_output != nullptr && indices_output != nullptr;
}

void configureTextureEnvironment(bool textured) {
  auto *environment = C3D_GetTexEnv(0);
  C3D_TexEnvInit(environment);
  if (textured) {
    C3D_TexEnvSrc(environment, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(environment, C3D_Both, GPU_MODULATE);
  } else {
    C3D_TexEnvSrc(environment, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(environment, C3D_Both, GPU_REPLACE);
  }
}

} // namespace

bool PreloginScene::initialize() {
  ground_tiles_ = C2D_SpriteSheetLoad("romfs:/prelogin_ground_tiles.t3x");
  skybox_ = C2D_SpriteSheetLoad("romfs:/prelogin_skybox.t3x");
  if (ground_tiles_ == nullptr || skybox_ == nullptr) {
    shutdown();
    return false;
  }

  const auto ground = C2D_SpriteSheetGetImage(ground_tiles_, 0);
  const auto sky = C2D_SpriteSheetGetImage(skybox_, 0);
  C3D_TexSetFilter(ground.tex, GPU_NEAREST, GPU_NEAREST);
  C3D_TexSetWrap(ground.tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
  C3D_TexSetFilter(sky.tex, GPU_NEAREST, GPU_NEAREST);
  C3D_TexSetWrap(sky.tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

  if (!loadShader() || !loadMeshes() ||
      !buildGroundMesh(ground, ground_vertices_, ground_indices_, ground_index_count_) ||
      !buildSkyMesh(sky, sky_vertices_, sky_indices_)) {
    shutdown();
    return false;
  }
  return true;
}

bool PreloginScene::loadShader() {
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
    shader_binary_.clear();
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

bool PreloginScene::loadMeshes() {
  office_vertices_ =
    copyToLinearMemory(generated::kOfficeVertices.data(), sizeof(generated::kOfficeVertices));
  office_indices_ =
    copyToLinearMemory(generated::kOfficeIndices.data(), sizeof(generated::kOfficeIndices));
  return office_vertices_ != nullptr && office_indices_ != nullptr && loadProductMark();
}

bool PreloginScene::loadProductMark() {
  auto *file = std::fopen("romfs:/prelogin_mark.o3m", "rb");
  if (file == nullptr) {
    return true;
  }

  std::array<char, 8> magic{};
  std::uint32_t vertex_count{};
  std::uint32_t index_count{};
  std::array<float, 6> bounds{};
  const auto valid_header =
    std::fread(magic.data(), 1, magic.size(), file) == magic.size() &&
    std::fread(&vertex_count, sizeof(vertex_count), 1, file) == 1 &&
    std::fread(&index_count, sizeof(index_count), 1, file) == 1 &&
    std::fread(bounds.data(), sizeof(float), bounds.size(), file) == bounds.size();
  if (!valid_header || magic != kProductMeshMagic || vertex_count == 0 || index_count == 0 ||
      vertex_count > kMaximumProductVertices || index_count > kMaximumProductIndices ||
      index_count % 3 != 0) {
    std::fclose(file);
    return false;
  }

  const auto vertex_bytes = static_cast<std::size_t>(vertex_count) * 28U;
  const auto index_bytes = static_cast<std::size_t>(index_count) * sizeof(std::uint16_t);
  mark_vertices_ = linearAlloc(vertex_bytes);
  mark_indices_ = linearAlloc(index_bytes);
  const auto read_mesh = mark_vertices_ != nullptr && mark_indices_ != nullptr &&
                         std::fread(mark_vertices_, 1, vertex_bytes, file) == vertex_bytes &&
                         std::fread(mark_indices_, 1, index_bytes, file) == index_bytes &&
                         std::fgetc(file) == EOF;
  std::fclose(file);
  if (!read_mesh) {
    return false;
  }

  const auto *indices = static_cast<const std::uint16_t *>(mark_indices_);
  for (std::uint32_t index = 0; index < index_count; ++index) {
    if (indices[index] >= vertex_count) {
      return false;
    }
  }
  mark_index_count_ = index_count;
  mark_minimum_y_ = bounds[1];
  return true;
}

void PreloginScene::shutdown() {
  const std::array<void **, 8> buffers{
    &office_vertices_, &office_indices_, &mark_vertices_, &mark_indices_,
    &ground_vertices_, &ground_indices_, &sky_vertices_,  &sky_indices_,
  };
  for (auto **buffer : buffers) {
    if (*buffer != nullptr) {
      linearFree(*buffer);
      *buffer = nullptr;
    }
  }
  ground_index_count_ = 0;
  mark_index_count_ = 0;
  mark_minimum_y_ = 0.0F;

  if (ground_tiles_ != nullptr) {
    C2D_SpriteSheetFree(ground_tiles_);
    ground_tiles_ = nullptr;
  }
  if (skybox_ != nullptr) {
    C2D_SpriteSheetFree(skybox_);
    skybox_ = nullptr;
  }
  if (shader_dvlb_ != nullptr) {
    shaderProgramFree(&shader_program_);
    DVLB_Free(shader_dvlb_);
    shader_dvlb_ = nullptr;
  }
  shader_binary_.clear();
  projection_uniform_ = -1;
  model_view_uniform_ = -1;
}

void PreloginScene::configureModelAttributes() {
  auto *attributes = C3D_GetAttrInfo();
  AttrInfo_Init(attributes);
  AttrInfo_AddLoader(attributes, 0, GPU_FLOAT, 3);
  AttrInfo_AddLoader(attributes, 1, GPU_FLOAT, 3);
  AttrInfo_AddLoader(attributes, 2, GPU_UNSIGNED_BYTE, 4);
  AttrInfo_AddFixed(attributes, 3);
  C3D_FixedAttribSet(3, 0.0F, 0.0F, 0.0F, 0.0F);
}

void PreloginScene::configureTexturedAttributes() {
  auto *attributes = C3D_GetAttrInfo();
  AttrInfo_Init(attributes);
  AttrInfo_AddLoader(attributes, 0, GPU_FLOAT, 3);
  AttrInfo_AddLoader(attributes, 1, GPU_FLOAT, 3);
  AttrInfo_AddLoader(attributes, 2, GPU_UNSIGNED_BYTE, 4);
  AttrInfo_AddLoader(attributes, 3, GPU_FLOAT, 2);
}

void PreloginScene::render(C3D_RenderTarget *target, float stereo_strength, bool right_eye) {
  C3D_RenderTargetClear(target, C3D_CLEAR_ALL, kClearColor, 0);
  C3D_FrameDrawOn(target);
  C3D_BindProgram(&shader_program_);
  C3D_CullFace(GPU_CULL_NONE);

  C3D_Mtx projection;
  C3D_Mtx model_view;
  Mtx_OrthoTilt(&projection, -200.0F, 200.0F, 0.0F, 240.0F, 0.0F, 1.0F, true);
  Mtx_Identity(&model_view);
  C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, projection_uniform_, &projection);
  C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, model_view_uniform_, &model_view);
  C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
  configureTexturedAttributes();
  auto *buffers = C3D_GetBufInfo();
  BufInfo_Init(buffers);
  BufInfo_Add(buffers, sky_vertices_, sizeof(SceneVertex), 4, 0x3210);
  C3D_TexBind(0, C2D_SpriteSheetGetImage(skybox_, 0).tex);
  configureTextureEnvironment(true);
  C3D_DrawElements(GPU_TRIANGLES, 6, C3D_UNSIGNED_SHORT, sky_indices_);

  const auto normalized_stereo = std::clamp(stereo_strength, 0.0F, 1.0F);
  const auto eye = (right_eye ? 1.0F : -1.0F) * kMaximumEyeDistance * normalized_stereo / 2.0F;
  const auto camera_position = FVec4_New(eye, 1.25F, -4.4F, 1.0F);
  const auto camera_target = FVec4_New(0.0F, 0.75F, kActorZ, 1.0F);
  const auto camera_up = FVec4_New(0.0F, 1.0F, 0.0F, 0.0F);
  C3D_Mtx view;
  Mtx_LookAt(&view, camera_position, camera_target, camera_up, false);
  Mtx_PerspTilt(&projection, kCameraFov, C3D_AspectRatioTop, 0.01F, 100.0F, false);
  C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, projection_uniform_, &projection);
  C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);

  model_view = view;
  C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, model_view_uniform_, &model_view);
  configureTexturedAttributes();
  buffers = C3D_GetBufInfo();
  BufInfo_Init(buffers);
  BufInfo_Add(buffers, ground_vertices_, sizeof(SceneVertex), 4, 0x3210);
  C3D_TexBind(0, C2D_SpriteSheetGetImage(ground_tiles_, 0).tex);
  configureTextureEnvironment(true);
  C3D_DrawElements(GPU_TRIANGLES, static_cast<int>(ground_index_count_), C3D_UNSIGNED_SHORT,
                   ground_indices_);

  configureModelAttributes();
  configureTextureEnvironment(false);
  model_view = view;
  Mtx_Translate(&model_view, 0.0F, -generated::kOfficeBoundsMinimum[1] * kModelScale, kActorZ,
                true);
  Mtx_Scale(&model_view, kModelScale, kModelScale, kModelScale);
  C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, model_view_uniform_, &model_view);
  buffers = C3D_GetBufInfo();
  BufInfo_Init(buffers);
  BufInfo_Add(buffers, office_vertices_, sizeof(generated::OfficeVertex), 3, 0x210);
  C3D_DrawElements(GPU_TRIANGLES, static_cast<int>(generated::kOfficeIndices.size()),
                   C3D_UNSIGNED_SHORT, office_indices_);

  if (mark_vertices_ != nullptr && mark_indices_ != nullptr && mark_index_count_ > 0) {
    model_view = view;
    Mtx_Translate(&model_view, kMarkX, -mark_minimum_y_ * kModelScale, kMarkZ, true);
    Mtx_RotateY(&model_view, C3D_AngleFromDegrees(180.0F), true);
    Mtx_Scale(&model_view, kModelScale, kModelScale, kModelScale);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, model_view_uniform_, &model_view);
    buffers = C3D_GetBufInfo();
    BufInfo_Init(buffers);
    BufInfo_Add(buffers, mark_vertices_, 28, 3, 0x210);
    C3D_DrawElements(GPU_TRIANGLES, static_cast<int>(mark_index_count_), C3D_UNSIGNED_SHORT,
                     mark_indices_);
  }
}

} // namespace office3ds::render_3d
