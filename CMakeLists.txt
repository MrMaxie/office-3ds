cmake_minimum_required(VERSION 3.25)

project(office_3ds VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(office_3ds_default_host_targets ${PROJECT_IS_TOP_LEVEL})
if(NINTENDO_3DS)
  set(office_3ds_default_host_targets OFF)
endif()
option(OFFICE_3DS_BUILD_CODEGEN "Build the native product generator" ${office_3ds_default_host_targets})
option(OFFICE_3DS_BUILD_DEMO_SERVER "Build the neutral SQLite demo server" ${office_3ds_default_host_targets})
option(OFFICE_3DS_BUILD_DEMO_PRODUCT "Generate the neutral demo product" ${PROJECT_IS_TOP_LEVEL})
option(OFFICE_3DS_BUILD_BRIDGE_HOST "Build the generated credential bridge" ${office_3ds_default_host_targets})
set(office_3ds_default_3ds_app OFF)
if(PROJECT_IS_TOP_LEVEL AND NINTENDO_3DS)
  set(office_3ds_default_3ds_app ON)
endif()
option(OFFICE_3DS_BUILD_3DS_APP "Build the generated Nintendo 3DS application" ${office_3ds_default_3ds_app})
set(OFFICE_3DS_CODEGEN_EXECUTABLE "" CACHE FILEPATH
    "Native office-3ds product generator used by a cross-compiling target build")

if(PROJECT_IS_TOP_LEVEL)
  include(CTest)
endif()
include(FetchContent)

add_library(office_3ds_api STATIC src/api/direct_api_client.cpp)
add_library(office3ds::api ALIAS office_3ds_api)
target_include_directories(office_3ds_api PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_compile_features(office_3ds_api PUBLIC cxx_std_17)

include(cmake/runtime.cmake)
include(cmake/platform.cmake)
include(cmake/generated_adapter_tests.cmake)

if(OFFICE_3DS_BUILD_CODEGEN)
  include(cmake/lua.cmake)
  add_executable(office_3ds_product_codegen
    src/product_codegen/generator.cpp
    src/product_codegen/lua_product_loader.cpp
    src/product_codegen/main.cpp
  )
  target_compile_features(office_3ds_product_codegen PRIVATE cxx_std_17)
  target_include_directories(office_3ds_product_codegen PRIVATE src/product_codegen)
  target_link_libraries(office_3ds_product_codegen PRIVATE office_3ds_lua)
  set_target_properties(office_3ds_product_codegen PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

include(cmake/Office3DSProduct.cmake)
if(OFFICE_3DS_BUILD_DEMO_PRODUCT)
  set(office_3ds_demo_values "${CMAKE_CURRENT_SOURCE_DIR}/products/demo/product-values.example.lua")
  office_3ds_add_product(
    PRODUCT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/products/demo"
    VALUES_FILE "${office_3ds_demo_values}"
    CODEGEN_EXECUTABLE "${OFFICE_3DS_CODEGEN_EXECUTABLE}"
    SLUG office-demo
  )
endif()

if(OFFICE_3DS_BUILD_3DS_APP)
  include(cmake/3ds.cmake)
endif()

if(OFFICE_3DS_BUILD_DEMO_SERVER OR OFFICE_3DS_BUILD_BRIDGE_HOST)
  if(NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(
      nlohmann_json
      GIT_REPOSITORY https://github.com/nlohmann/json.git
      GIT_TAG 65ee68451d8eb2b5f3a30b410476ab83deb3289b
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
  endif()
  if(NOT TARGET httplib::httplib)
    FetchContent_Declare(
      cpp_httplib
      GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
      GIT_TAG 787a34ad7f01f20922a237d5142aae469828be72
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(cpp_httplib)
  endif()
  if(OFFICE_3DS_BUILD_BRIDGE_HOST AND NOT TARGET office_3ds_qrcodegen)
    FetchContent_Declare(
      office_3ds_qrcodegen_source
      GIT_REPOSITORY https://github.com/nayuki/QR-Code-generator.git
      GIT_TAG 7ad95cedd8464a87f82221283612732ae4f3f305
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(office_3ds_qrcodegen_source)
    add_library(office_3ds_qrcodegen STATIC
      "${office_3ds_qrcodegen_source_SOURCE_DIR}/cpp/qrcodegen.cpp")
    target_include_directories(office_3ds_qrcodegen PUBLIC
      "${office_3ds_qrcodegen_source_SOURCE_DIR}/cpp")
  endif()
endif()

if(OFFICE_3DS_BUILD_DEMO_SERVER)
  include(cmake/demo_server.cmake)
endif()

if(OFFICE_3DS_BUILD_BRIDGE_HOST)
  include(cmake/bridge_host.cmake)
endif()

if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
  add_executable(office_3ds_bitmap_font_scale_tests tests/bitmap_font_scale_tests.cpp)
  target_include_directories(office_3ds_bitmap_font_scale_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
  add_test(NAME office_3ds_bitmap_font_scale_tests COMMAND office_3ds_bitmap_font_scale_tests)

  add_executable(office_3ds_api_contract_tests tests/api_contract_tests.cpp)
  target_link_libraries(office_3ds_api_contract_tests PRIVATE office3ds::api)
  add_test(NAME office_3ds_api_contract_tests COMMAND office_3ds_api_contract_tests)

  add_executable(office_3ds_direct_api_client_tests tests/direct_api_client_tests.cpp)
  target_link_libraries(office_3ds_direct_api_client_tests PRIVATE office3ds::api)
  add_test(NAME office_3ds_direct_api_client_tests COMMAND office_3ds_direct_api_client_tests)

  add_executable(office_3ds_product_codegen_tests
    tests/product_codegen_tests.cpp
    src/product_codegen/generator.cpp
    src/product_codegen/lua_product_loader.cpp
  )
  target_compile_features(office_3ds_product_codegen_tests PRIVATE cxx_std_17)
  target_include_directories(office_3ds_product_codegen_tests PRIVATE src/product_codegen)
  target_link_libraries(office_3ds_product_codegen_tests PRIVATE office_3ds_lua)
  add_test(
    NAME office_3ds_product_codegen_tests
    COMMAND office_3ds_product_codegen_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/products/demo/product.lua"
      "${CMAKE_CURRENT_SOURCE_DIR}/products/demo/product-values.example.lua"
  )
endif()
