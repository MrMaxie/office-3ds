include(FetchContent)

if(TARGET office_3ds_runtime)
  return()
endif()

get_filename_component(office_3ds_runtime_root
  "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(NOT TARGET nlohmann_json::nlohmann_json)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG 65ee68451d8eb2b5f3a30b410476ab83deb3289b
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

if(NOT TARGET office_3ds_monocypher)
  FetchContent_Declare(
    office_3ds_monocypher_source
    GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
    GIT_TAG ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(office_3ds_monocypher_source)
  add_library(office_3ds_monocypher STATIC
    "${office_3ds_monocypher_source_SOURCE_DIR}/src/monocypher.c")
  target_include_directories(office_3ds_monocypher PUBLIC
    "${office_3ds_monocypher_source_SOURCE_DIR}/src")
endif()

add_library(office_3ds_runtime STATIC
  "${office_3ds_runtime_root}/src/core/application_flow.cpp"
  "${office_3ds_runtime_root}/src/core/credential_bundle.cpp"
  "${office_3ds_runtime_root}/src/core/dashboard_state.cpp"
  "${office_3ds_runtime_root}/src/core/pairing_entry.cpp"
  "${office_3ds_runtime_root}/src/bridge/credential_claim.cpp"
  "${office_3ds_runtime_root}/src/bridge/pairing.cpp"
  "${office_3ds_runtime_root}/src/bridge/pairing_session.cpp"
)
add_library(office3ds::runtime ALIAS office_3ds_runtime)
target_compile_features(office_3ds_runtime PUBLIC cxx_std_17)
target_include_directories(office_3ds_runtime PUBLIC
  "${office_3ds_runtime_root}/include")
target_link_libraries(office_3ds_runtime
  PUBLIC office3ds::api
  PRIVATE office_3ds_monocypher nlohmann_json::nlohmann_json)
if(WIN32)
  target_link_libraries(office_3ds_runtime PRIVATE bcrypt)
endif()

if(BUILD_TESTING AND (PROJECT_IS_TOP_LEVEL OR OFFICE_3DS_RUNTIME_BUILD_TESTS))
  add_executable(office_3ds_runtime_tests
    "${office_3ds_runtime_root}/tests/runtime/runtime_tests.cpp")
  target_compile_features(office_3ds_runtime_tests PRIVATE cxx_std_17)
  target_link_libraries(office_3ds_runtime_tests PRIVATE
    office3ds::runtime office_3ds_monocypher)
  add_test(NAME office_3ds_runtime_tests COMMAND office_3ds_runtime_tests)
endif()
