cmake_minimum_required(VERSION 3.25)

project(office_3ds_consumer LANGUAGES CXX)

add_subdirectory("../../.." office-3ds)

if(NOT TARGET office3ds::api)
  message(FATAL_ERROR "office3ds::api target is unavailable to a downstream consumer")
endif()
if(TARGET office_3ds_product_codegen OR TARGET office_3ds_demo_server)
  message(FATAL_ERROR "host tools and demo targets must be opt-in downstream")
endif()

add_executable(office_3ds_consumer main.cpp)
target_link_libraries(office_3ds_consumer PRIVATE office3ds::api)
