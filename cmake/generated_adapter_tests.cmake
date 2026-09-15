if(TARGET office_3ds_generated_adapter)
  return()
endif()

if(NOT TARGET office3ds::api)
  message(FATAL_ERROR "generated adapter requires the office3ds::api target")
endif()
if(NOT TARGET nlohmann_json::nlohmann_json)
  message(FATAL_ERROR "generated adapter requires the nlohmann_json::nlohmann_json target")
endif()

get_filename_component(office_3ds_generated_adapter_root
  "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

add_library(office_3ds_generated_adapter STATIC
  "${office_3ds_generated_adapter_root}/src/generated_adapter/demo_json_codec.cpp"
  "${office_3ds_generated_adapter_root}/src/generated_adapter/generated_adapter.cpp"
)
add_library(office3ds::generated_adapter ALIAS office_3ds_generated_adapter)
target_compile_features(office_3ds_generated_adapter PUBLIC cxx_std_17)
target_include_directories(office_3ds_generated_adapter PUBLIC
  "${office_3ds_generated_adapter_root}/include")
target_link_libraries(office_3ds_generated_adapter
  PUBLIC office3ds::api
  PRIVATE nlohmann_json::nlohmann_json)

function(office_3ds_link_generated_adapter target_name)
  if(NOT TARGET "${target_name}")
    message(FATAL_ERROR "cannot link generated adapter to missing target: ${target_name}")
  endif()
  target_link_libraries("${target_name}" PUBLIC office3ds::generated_adapter)
  if(BUILD_TESTING AND NOT TARGET office_3ds_generated_factory_tests)
    add_executable(office_3ds_generated_factory_tests
      "${office_3ds_generated_adapter_root}/tests/generated_adapter/generated_factory_tests.cpp")
    target_compile_features(office_3ds_generated_factory_tests PRIVATE cxx_std_17)
    target_link_libraries(office_3ds_generated_factory_tests PRIVATE
      "${target_name}"
      office3ds::generated_adapter)
    add_test(NAME office_3ds_generated_factory_tests
      COMMAND office_3ds_generated_factory_tests)
  endif()
endfunction()

function(office_3ds_add_generated_adapter_late_tests)
  if(NOT BUILD_TESTING OR NOT PROJECT_IS_TOP_LEVEL)
    return()
  endif()
  if(DEFINED OFFICE_3DS_PRODUCT_TARGET AND
      TARGET "${OFFICE_3DS_PRODUCT_TARGET}" AND
      NOT TARGET office_3ds_generated_factory_tests)
    add_executable(office_3ds_generated_factory_tests
      "${office_3ds_generated_adapter_root}/tests/generated_adapter/generated_factory_tests.cpp")
    target_compile_features(office_3ds_generated_factory_tests PRIVATE cxx_std_17)
    target_link_libraries(office_3ds_generated_factory_tests PRIVATE
      "${OFFICE_3DS_PRODUCT_TARGET}"
      office3ds::generated_adapter)
    add_test(NAME office_3ds_generated_factory_tests
      COMMAND office_3ds_generated_factory_tests)
  endif()
  if(TARGET office_3ds_demo_server_lib AND
      NOT TARGET office_3ds_generated_adapter_server_integration_tests)
    add_executable(office_3ds_generated_adapter_server_integration_tests
      "${office_3ds_generated_adapter_root}/tests/generated_adapter/generated_adapter_server_integration_tests.cpp")
    target_compile_features(office_3ds_generated_adapter_server_integration_tests PRIVATE cxx_std_17)
    target_link_libraries(office_3ds_generated_adapter_server_integration_tests PRIVATE
      office3ds::generated_adapter
      office_3ds_demo_server_lib
      "${OFFICE_3DS_PRODUCT_TARGET}")
    add_test(NAME office_3ds_generated_adapter_server_integration_tests
      COMMAND office_3ds_generated_adapter_server_integration_tests)
  endif()
endfunction()

if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
  add_executable(office_3ds_generated_adapter_tests
    "${office_3ds_generated_adapter_root}/tests/generated_adapter/generated_adapter_tests.cpp")
  target_compile_features(office_3ds_generated_adapter_tests PRIVATE cxx_std_17)
  target_link_libraries(office_3ds_generated_adapter_tests PRIVATE
    office3ds::generated_adapter
    nlohmann_json::nlohmann_json)
  add_test(NAME office_3ds_generated_adapter_tests
    COMMAND office_3ds_generated_adapter_tests)

  add_executable(office_3ds_generated_output_tests
    "${office_3ds_generated_adapter_root}/tests/generated_adapter/generated_output_tests.cpp"
    "${office_3ds_generated_adapter_root}/src/product_codegen/generator.cpp")
  target_compile_features(office_3ds_generated_output_tests PRIVATE cxx_std_17)
  target_include_directories(office_3ds_generated_output_tests PRIVATE
    "${office_3ds_generated_adapter_root}/src/product_codegen")
  add_test(NAME office_3ds_generated_output_tests
    COMMAND office_3ds_generated_output_tests)

endif()

cmake_language(DEFER CALL office_3ds_add_generated_adapter_late_tests)
