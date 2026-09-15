cmake_minimum_required(VERSION 3.25)

project(office_3ds_runtime_standalone LANGUAGES C CXX)

include(CTest)
set(OFFICE_3DS_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
set(OFFICE_3DS_BUILD_DEMO_PRODUCT OFF CACHE BOOL "" FORCE)
set(OFFICE_3DS_BUILD_DEMO_SERVER OFF CACHE BOOL "" FORCE)
set(OFFICE_3DS_RUNTIME_BUILD_TESTS ON)
add_subdirectory("../.." office-3ds)
include("../../cmake/runtime.cmake")
