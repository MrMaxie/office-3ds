cmake_minimum_required(VERSION 3.25)

project(office_3ds_platform_standalone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

get_filename_component(office_3ds_root
  "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_library(office_3ds_api STATIC
  "${office_3ds_root}/src/api/direct_api_client.cpp"
)
add_library(office3ds::api ALIAS office_3ds_api)
target_include_directories(office_3ds_api PUBLIC
  "${office_3ds_root}/include"
)

include("${office_3ds_root}/cmake/platform.cmake")
