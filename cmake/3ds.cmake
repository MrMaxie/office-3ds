if(NOT NINTENDO_3DS)
  return()
endif()
if(NOT DEFINED OFFICE_3DS_PRODUCT_TARGET OR NOT TARGET "${OFFICE_3DS_PRODUCT_TARGET}")
  message(FATAL_ERROR "A generated product target is required for the Nintendo 3DS application")
endif()

include(FetchContent)
FetchContent_Declare(
  office_3ds_quirc_source
  GIT_REPOSITORY https://github.com/dlbeer/quirc.git
  GIT_TAG 542848dd6b9b0eaa9587bbf25b9bc67bd8a71fca
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(office_3ds_quirc_source)

add_library(office_3ds_quirc STATIC
  "${office_3ds_quirc_source_SOURCE_DIR}/lib/decode.c"
  "${office_3ds_quirc_source_SOURCE_DIR}/lib/identify.c"
  "${office_3ds_quirc_source_SOURCE_DIR}/lib/quirc.c"
  "${office_3ds_quirc_source_SOURCE_DIR}/lib/version_db.c"
)
target_include_directories(office_3ds_quirc PUBLIC
  "${office_3ds_quirc_source_SOURCE_DIR}/lib")
target_link_libraries(office_3ds_quirc PRIVATE m)

find_package(PkgConfig REQUIRED)
pkg_check_modules(OFFICE_3DS_CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(OFFICE_3DS_AVATAR_CODECS REQUIRED IMPORTED_TARGET libpng libturbojpeg)

add_library(office_3ds_device STATIC
  "${CMAKE_CURRENT_LIST_DIR}/../src/app/bitmap_font.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/app/dashboard_app.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/app/login_controls.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/app/native_theme.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/app/session_views.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/platform_3ds/audio_feedback.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/platform_3ds/avatar_cache_store.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/platform_3ds/avatar_store.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/platform_3ds/bridge_claim_source.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/platform_3ds/credential_store.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/platform_3ds/curl_download.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/platform_3ds/http.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/platform_3ds/qr_scanner.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/render_3d/prelogin_scene.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/render_3d/worklog_scene.cpp"
)
add_library(office3ds::device ALIAS office_3ds_device)
target_compile_features(office_3ds_device PUBLIC cxx_std_17)
target_include_directories(office_3ds_device PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../include>"
  PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../src/app")
target_compile_options(office_3ds_device PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(office_3ds_device PUBLIC
  citro2d
  citro3d
  PkgConfig::OFFICE_3DS_AVATAR_CODECS
  PkgConfig::OFFICE_3DS_CURL
  office3ds::platform
  office3ds::runtime
  office_3ds_quirc
  office_3ds_monocypher
)

add_executable(office_3ds_app
  "${CMAKE_CURRENT_LIST_DIR}/../src/platform_3ds/main.cpp")
target_compile_features(office_3ds_app PRIVATE cxx_std_17)
target_compile_options(office_3ds_app PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(office_3ds_app PRIVATE
  office3ds::device
  "${OFFICE_3DS_PRODUCT_TARGET}"
)
add_custom_command(TARGET office_3ds_app POST_BUILD
  COMMAND "${CMAKE_COMMAND}"
    -DOFFICE_3DS_RUNTIME_BINARY=$<TARGET_FILE:office_3ds_app>
    -P "${CMAKE_CURRENT_LIST_DIR}/verify_no_lua_binary.cmake"
  COMMENT "Verifying that the Nintendo 3DS runtime contains no Lua input"
  VERBATIM
)

set(office_3ds_icon_asset "")
set(office_3ds_prelogin_ground_asset "")
set(office_3ds_prelogin_skybox_asset "")
set(office_3ds_certificate_bundle_asset "")
set(office_3ds_prelogin_background_asset "")
set(office_3ds_login_background_asset "")
set(office_3ds_top_background_asset "")
set(office_3ds_bottom_background_asset "")
set(office_3ds_prelogin_mark_mesh_asset "")
foreach(office_3ds_asset IN LISTS OFFICE_3DS_GENERATED_ASSETS)
  if(office_3ds_asset MATCHES "^icon=(.+)$")
    set(office_3ds_icon_asset "${OFFICE_3DS_PRODUCT_DIR}/${CMAKE_MATCH_1}")
  elseif(office_3ds_asset MATCHES "^prelogin_ground=(.+)$")
    set(office_3ds_prelogin_ground_asset "${OFFICE_3DS_PRODUCT_DIR}/${CMAKE_MATCH_1}")
  elseif(office_3ds_asset MATCHES "^prelogin_skybox=(.+)$")
    set(office_3ds_prelogin_skybox_asset "${OFFICE_3DS_PRODUCT_DIR}/${CMAKE_MATCH_1}")
  elseif(office_3ds_asset MATCHES "^certificate_bundle=(.+)$")
    set(office_3ds_certificate_bundle_asset "${OFFICE_3DS_PRODUCT_DIR}/${CMAKE_MATCH_1}")
  elseif(office_3ds_asset MATCHES "^prelogin_background=(.+)$")
    set(office_3ds_prelogin_background_asset "${OFFICE_3DS_PRODUCT_DIR}/${CMAKE_MATCH_1}")
  elseif(office_3ds_asset MATCHES "^login_background=(.+)$")
    set(office_3ds_login_background_asset "${OFFICE_3DS_PRODUCT_DIR}/${CMAKE_MATCH_1}")
  elseif(office_3ds_asset MATCHES "^top_background=(.+)$")
    set(office_3ds_top_background_asset "${OFFICE_3DS_PRODUCT_DIR}/${CMAKE_MATCH_1}")
  elseif(office_3ds_asset MATCHES "^bottom_background=(.+)$")
    set(office_3ds_bottom_background_asset "${OFFICE_3DS_PRODUCT_DIR}/${CMAKE_MATCH_1}")
  elseif(office_3ds_asset MATCHES "^prelogin_mark_mesh=(.+)$")
    set(office_3ds_prelogin_mark_mesh_asset "${OFFICE_3DS_PRODUCT_DIR}/${CMAKE_MATCH_1}")
  endif()
endforeach()
if(NOT office_3ds_icon_asset OR NOT EXISTS "${office_3ds_icon_asset}")
  message(FATAL_ERROR "The generated product does not provide a valid Nintendo 3DS icon asset")
endif()
if(NOT office_3ds_prelogin_ground_asset OR
   NOT EXISTS "${office_3ds_prelogin_ground_asset}")
  message(FATAL_ERROR "The generated product does not provide a valid prelogin ground asset")
endif()
if(NOT office_3ds_prelogin_skybox_asset OR
   NOT EXISTS "${office_3ds_prelogin_skybox_asset}")
  message(FATAL_ERROR "The generated product does not provide a valid prelogin skybox asset")
endif()
foreach(office_3ds_required_background IN ITEMS
        prelogin_background login_background top_background bottom_background)
  if(NOT office_3ds_${office_3ds_required_background}_asset OR
     NOT EXISTS "${office_3ds_${office_3ds_required_background}_asset}")
    message(FATAL_ERROR
      "The generated product does not provide a valid ${office_3ds_required_background} asset")
  endif()
endforeach()

set(office_3ds_romfs_dir "${CMAKE_CURRENT_BINARY_DIR}/romfs")
file(MAKE_DIRECTORY "${office_3ds_romfs_dir}")
if(office_3ds_prelogin_mark_mesh_asset)
  if(NOT EXISTS "${office_3ds_prelogin_mark_mesh_asset}")
    message(FATAL_ERROR "The generated product references a missing prelogin mark mesh")
  endif()
  configure_file(
    "${office_3ds_prelogin_mark_mesh_asset}"
    "${office_3ds_romfs_dir}/prelogin_mark.o3m"
    COPYONLY
  )
endif()
if(office_3ds_certificate_bundle_asset)
  if(NOT EXISTS "${office_3ds_certificate_bundle_asset}")
    message(FATAL_ERROR "The generated product references a missing certificate bundle")
  endif()
  configure_file(
    "${office_3ds_certificate_bundle_asset}"
    "${office_3ds_romfs_dir}/https-trust-roots.pem"
    COPYONLY
  )
endif()
ctr_add_graphics_target(office_3ds_kenney_pixel_font IMAGE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/kenney_pixel_font.t3x"
  INPUTS "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/kenney-pixel-10x16.png"
)
ctr_add_graphics_target(office_3ds_prelogin_ground IMAGE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/prelogin_ground_tiles.t3x"
  INPUTS "${office_3ds_prelogin_ground_asset}"
)
ctr_add_graphics_target(office_3ds_prelogin_skybox IMAGE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/prelogin_skybox.t3x"
  INPUTS "${office_3ds_prelogin_skybox_asset}"
)
ctr_add_graphics_target(office_3ds_prelogin_background IMAGE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/prelogin_background.t3x"
  INPUTS "${office_3ds_prelogin_background_asset}"
)
ctr_add_graphics_target(office_3ds_login_background IMAGE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/login_background.t3x"
  INPUTS "${office_3ds_login_background_asset}"
)
ctr_add_graphics_target(office_3ds_top_background IMAGE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/general_top_background.t3x"
  INPUTS "${office_3ds_top_background_asset}"
)
ctr_add_graphics_target(office_3ds_bottom_background IMAGE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/general_bottom_background.t3x"
  INPUTS "${office_3ds_bottom_background_asset}"
)
ctr_add_shader_library(office_3ds_prelogin_scene_shader
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/prelogin_scene.shbin"
  "${CMAKE_CURRENT_LIST_DIR}/../src/render_3d/prelogin_scene.v.pica"
)
dkp_add_asset_target(office_3ds_romfs_assets "${office_3ds_romfs_dir}")
dkp_install_assets(office_3ds_romfs_assets TARGETS
  office_3ds_kenney_pixel_font
  office_3ds_prelogin_ground
  office_3ds_prelogin_skybox
  office_3ds_prelogin_background
  office_3ds_login_background
  office_3ds_top_background
  office_3ds_bottom_background
  office_3ds_prelogin_scene_shader
)

set(office_3ds_smdh "${CMAKE_CURRENT_BINARY_DIR}/${OFFICE_3DS_GENERATED_OUTPUT_BASENAME}.smdh")
ctr_generate_smdh(
  OUTPUT "${office_3ds_smdh}"
  NAME "${OFFICE_3DS_GENERATED_DISPLAY_NAME}"
  DESCRIPTION "${OFFICE_3DS_GENERATED_DESCRIPTION}"
  AUTHOR "${OFFICE_3DS_GENERATED_AUTHOR}"
  ICON "${office_3ds_icon_asset}"
)

if(OFFICE_3DS_3DSX_OUTPUT_DIRECTORY)
  set(office_3ds_dist_dir "${OFFICE_3DS_3DSX_OUTPUT_DIRECTORY}")
else()
  set(office_3ds_dist_dir "${CMAKE_CURRENT_LIST_DIR}/../dist/3ds")
endif()
file(MAKE_DIRECTORY "${office_3ds_dist_dir}")
set(office_3ds_3dsx
  "${office_3ds_dist_dir}/${OFFICE_3DS_GENERATED_OUTPUT_BASENAME}.3dsx")
ctr_create_3dsx(office_3ds_app
  OUTPUT "${office_3ds_3dsx}"
  SMDH "${office_3ds_smdh}"
  ROMFS office_3ds_romfs_assets
)
add_custom_target(office_3ds_product_3ds ALL DEPENDS office_3ds_app_3dsx)
