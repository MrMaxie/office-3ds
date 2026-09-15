if(TARGET office_3ds_bridge_host)
  return()
endif()

if(NOT TARGET office3ds::runtime OR NOT TARGET office_3ds_monocypher)
  message(FATAL_ERROR "bridge host requires the office-3ds runtime and Monocypher targets")
endif()
if(NOT TARGET httplib::httplib OR NOT TARGET nlohmann_json::nlohmann_json)
  message(FATAL_ERROR "bridge host requires cpp-httplib and nlohmann/json")
endif()

get_filename_component(office_3ds_bridge_host_root
  "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

add_library(office_3ds_bridge_host STATIC
  "${office_3ds_bridge_host_root}/src/bridge_host/credential_source.cpp"
  "${office_3ds_bridge_host_root}/src/bridge_host/pairing_status_page.cpp"
  "${office_3ds_bridge_host_root}/src/bridge_host/server.cpp"
)
add_library(office3ds::bridge_host ALIAS office_3ds_bridge_host)
target_compile_features(office_3ds_bridge_host PUBLIC cxx_std_17)
target_include_directories(office_3ds_bridge_host PUBLIC
  "${office_3ds_bridge_host_root}/include")
target_link_libraries(office_3ds_bridge_host
  PUBLIC office3ds::runtime
  PRIVATE
    office_3ds_monocypher
    office_3ds_qrcodegen
    httplib::httplib
    nlohmann_json::nlohmann_json
)
if(WIN32)
  target_link_libraries(office_3ds_bridge_host PRIVATE bcrypt)
endif()

if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
  add_executable(office_3ds_credential_source_tests
    "${office_3ds_bridge_host_root}/tests/bridge_host/credential_source_tests.cpp")
  target_compile_features(office_3ds_credential_source_tests PRIVATE cxx_std_17)
  target_link_libraries(office_3ds_credential_source_tests PRIVATE
    office3ds::bridge_host
    nlohmann_json::nlohmann_json)
  add_test(NAME office_3ds_credential_source_tests
    COMMAND office_3ds_credential_source_tests)
endif()

function(office_3ds_assert_runtime_target_has_no_lua target_name)
  set(queue "${target_name}")
  set(visited)
  while(queue)
    list(POP_FRONT queue current)
    if(current MATCHES "(^|::)office_3ds_lua$" OR current MATCHES "\\.lua($|[>;])")
      message(FATAL_ERROR "Runtime target ${target_name} reaches forbidden Lua input: ${current}")
    endif()
    if(NOT TARGET "${current}" OR current IN_LIST visited)
      continue()
    endif()
    list(APPEND visited "${current}")
    get_target_property(aliased "${current}" ALIASED_TARGET)
    if(aliased)
      list(APPEND queue "${aliased}")
      continue()
    endif()
    get_target_property(sources "${current}" SOURCES)
    foreach(source IN LISTS sources)
      if(source MATCHES "\\.lua$")
        message(FATAL_ERROR
          "Runtime target ${target_name} contains forbidden Lua source: ${source}")
      endif()
    endforeach()
    get_target_property(private_links "${current}" LINK_LIBRARIES)
    get_target_property(interface_links "${current}" INTERFACE_LINK_LIBRARIES)
    foreach(link IN LISTS private_links interface_links)
      if(link MATCHES "office_3ds_lua" OR link MATCHES "\\.lua($|[>:])")
        message(FATAL_ERROR
          "Runtime target ${target_name} reaches forbidden Lua input: ${link}")
      endif()
      if(link MATCHES "^\\$<")
        continue()
      endif()
      list(APPEND queue "${link}")
    endforeach()
  endwhile()
endfunction()

if(DEFINED OFFICE_3DS_PRODUCT_TARGET AND TARGET "${OFFICE_3DS_PRODUCT_TARGET}")
  add_executable(office_3ds_bridge
    "${office_3ds_bridge_host_root}/src/bridge_host/main.cpp")
  target_compile_features(office_3ds_bridge PRIVATE cxx_std_17)
  target_link_libraries(office_3ds_bridge PRIVATE
    office3ds::bridge_host
    "${OFFICE_3DS_PRODUCT_TARGET}"
  )
  office_3ds_assert_runtime_target_has_no_lua(office_3ds_bridge)

  if(BUILD_TESTING)
    add_test(NAME office_3ds_bridge_binary_contract
      COMMAND "${CMAKE_COMMAND}"
        "-DOFFICE_3DS_RUNTIME_BINARY=$<TARGET_FILE:office_3ds_bridge>"
        -P "${office_3ds_bridge_host_root}/cmake/verify_no_lua_binary.cmake")

    if(TARGET office_3ds_demo_server_lib)
      add_executable(office_3ds_bridge_host_integration_tests
        "${office_3ds_bridge_host_root}/tests/bridge_host/bridge_host_integration_tests.cpp")
      target_compile_features(office_3ds_bridge_host_integration_tests PRIVATE cxx_std_17)
      target_link_libraries(office_3ds_bridge_host_integration_tests PRIVATE
        office3ds::bridge_host
        office_3ds_demo_server_lib
        office_3ds_monocypher
        "${OFFICE_3DS_PRODUCT_TARGET}"
        httplib::httplib
        nlohmann_json::nlohmann_json
      )
      add_test(NAME office_3ds_bridge_host_integration_tests
        COMMAND office_3ds_bridge_host_integration_tests)
    endif()
  endif()
endif()
