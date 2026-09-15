include(FetchContent)

if(TARGET office_3ds_demo_server)
  return()
endif()

if(NOT TARGET httplib::httplib)
  message(FATAL_ERROR "office_3ds_demo_server requires the httplib::httplib target")
endif()
if(NOT TARGET nlohmann_json::nlohmann_json)
  message(FATAL_ERROR
    "office_3ds_demo_server requires the nlohmann_json::nlohmann_json target")
endif()

enable_language(C)
find_package(Threads REQUIRED)
get_filename_component(office_3ds_demo_root
  "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# SQLite 3.53.3 official amalgamation. The URL and archive SHA-256 are
# immutable so configuring the demo backend is reproducible.
FetchContent_Declare(
  office_3ds_sqlite
  URL https://sqlite.org/2026/sqlite-amalgamation-3530300.zip
  URL_HASH SHA256=646421e12aac110282ef8cc68f1a62d4bb15fc7b8f09da0b53e29ee690500431
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(office_3ds_sqlite)

file(GLOB_RECURSE office_3ds_sqlite_sources CONFIGURE_DEPENDS
  "${office_3ds_sqlite_SOURCE_DIR}/sqlite3.c")
list(LENGTH office_3ds_sqlite_sources office_3ds_sqlite_source_count)
if(NOT office_3ds_sqlite_source_count EQUAL 1)
  message(FATAL_ERROR "Expected exactly one sqlite3.c in the pinned amalgamation")
endif()
list(GET office_3ds_sqlite_sources 0 office_3ds_sqlite_source)
get_filename_component(office_3ds_sqlite_include_dir
  "${office_3ds_sqlite_source}" DIRECTORY)

add_library(office_3ds_demo_sqlite STATIC "${office_3ds_sqlite_source}")
target_include_directories(office_3ds_demo_sqlite PUBLIC
  "${office_3ds_sqlite_include_dir}")
target_compile_definitions(office_3ds_demo_sqlite PRIVATE
  SQLITE_DEFAULT_FOREIGN_KEYS=1
  SQLITE_DQS=0
  SQLITE_OMIT_DEPRECATED=1
  SQLITE_OMIT_LOAD_EXTENSION=1
  SQLITE_THREADSAFE=1
)

add_library(office_3ds_demo_server_lib STATIC
  "${office_3ds_demo_root}/src/demo_server/demo_service.cpp"
  "${office_3ds_demo_root}/src/demo_server/server.cpp"
  "${office_3ds_demo_root}/src/demo_server/sha256.cpp"
)
target_compile_features(office_3ds_demo_server_lib PUBLIC cxx_std_17)
target_include_directories(office_3ds_demo_server_lib PUBLIC
  "${office_3ds_demo_root}/include")
target_link_libraries(office_3ds_demo_server_lib
  PUBLIC
    httplib::httplib
    nlohmann_json::nlohmann_json
  PRIVATE
    office_3ds_demo_sqlite
    Threads::Threads
)
if(WIN32)
  target_link_libraries(office_3ds_demo_server_lib PRIVATE advapi32 bcrypt)
endif()

add_executable(office_3ds_demo_server
  "${office_3ds_demo_root}/src/demo_server/main.cpp")
target_compile_features(office_3ds_demo_server PRIVATE cxx_std_17)
target_link_libraries(office_3ds_demo_server PRIVATE
  office_3ds_demo_server_lib)

if(BUILD_TESTING)
  add_executable(office_3ds_demo_server_tests
    "${office_3ds_demo_root}/tests/demo_server/demo_server_tests.cpp")
  target_compile_features(office_3ds_demo_server_tests PRIVATE cxx_std_17)
  target_link_libraries(office_3ds_demo_server_tests PRIVATE
    office_3ds_demo_server_lib
    office_3ds_demo_sqlite)
  add_test(NAME office_3ds_demo_server_tests
    COMMAND office_3ds_demo_server_tests)
endif()
