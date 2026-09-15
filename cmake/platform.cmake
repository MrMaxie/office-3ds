get_filename_component(office_3ds_platform_root
  "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

add_library(office_3ds_platform STATIC
  "${office_3ds_platform_root}/src/platform/credential.cpp"
  "${office_3ds_platform_root}/src/platform/resource_owner.cpp"
  "${office_3ds_platform_root}/src/platform/session.cpp"
)
add_library(office3ds::platform ALIAS office_3ds_platform)
target_include_directories(office_3ds_platform PUBLIC
  "$<BUILD_INTERFACE:${office_3ds_platform_root}/include>"
)
target_compile_features(office_3ds_platform PUBLIC cxx_std_17)
target_link_libraries(office_3ds_platform PUBLIC office3ds::api office3ds::runtime)

if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
  add_executable(office_3ds_platform_tests
    "${office_3ds_platform_root}/tests/platform/platform_lifecycle_tests.cpp"
  )
  target_link_libraries(office_3ds_platform_tests PRIVATE office3ds::platform)
  add_test(NAME office_3ds_platform_tests COMMAND office_3ds_platform_tests)
endif()
