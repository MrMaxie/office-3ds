include(CMakeParseArguments)

function(office_3ds_add_product)
  cmake_parse_arguments(PRODUCT "" "PRODUCT_DIR;VALUES_FILE;CODEGEN_EXECUTABLE;SLUG" "" ${ARGN})
  if(PRODUCT_UNPARSED_ARGUMENTS OR NOT PRODUCT_PRODUCT_DIR OR NOT PRODUCT_SLUG)
    message(FATAL_ERROR
      "office_3ds_add_product requires PRODUCT_DIR and SLUG and accepts optional VALUES_FILE and CODEGEN_EXECUTABLE")
  endif()
  if(NOT PRODUCT_SLUG MATCHES "^[a-z0-9-]+$")
    message(FATAL_ERROR "Product SLUG must contain only lowercase letters, digits, and hyphens")
  endif()

  cmake_path(ABSOLUTE_PATH PRODUCT_PRODUCT_DIR NORMALIZE OUTPUT_VARIABLE product_dir)
  set(product_file "${product_dir}/product.lua")
  if(NOT EXISTS "${product_file}")
    message(FATAL_ERROR "Product directory does not contain product.lua")
  endif()

  if(PRODUCT_CODEGEN_EXECUTABLE)
    set(codegen_command "${PRODUCT_CODEGEN_EXECUTABLE}")
    set(codegen_dependency "${PRODUCT_CODEGEN_EXECUTABLE}")
  elseif(TARGET office_3ds_product_codegen)
    set(codegen_command "$<TARGET_FILE:office_3ds_product_codegen>")
    set(codegen_dependency office_3ds_product_codegen)
  else()
    message(FATAL_ERROR
      "No native codegen available; pass CODEGEN_EXECUTABLE when cross-compiling")
  endif()

  set(generated_root "${CMAKE_CURRENT_BINARY_DIR}/generated/product")
  set(generated_dir "${generated_root}/${PRODUCT_SLUG}")
  set(generated_outputs
    "${generated_dir}/product_config.hpp"
    "${generated_dir}/product_config.cpp"
    "${generated_dir}/product_requests.cpp"
    "${generated_dir}/product_mapper.cpp"
    "${generated_dir}/product_adapter_factory.cpp"
    "${generated_dir}/product_assets.cmake"
  )
  set(command_args
    --product "${product_file}"
    --expected-slug "${PRODUCT_SLUG}"
    --output "${generated_root}")
  set(value_dependency)
  if(PRODUCT_VALUES_FILE)
    cmake_path(ABSOLUTE_PATH PRODUCT_VALUES_FILE NORMALIZE OUTPUT_VARIABLE values_file)
    list(APPEND command_args --values "${values_file}")
    set(value_dependency "${values_file}")
  endif()

  if(PRODUCT_CODEGEN_EXECUTABLE)
    execute_process(
      COMMAND "${codegen_command}" ${command_args}
      RESULT_VARIABLE configure_codegen_result
      OUTPUT_QUIET
      ERROR_QUIET
    )
    if(NOT configure_codegen_result EQUAL 0)
      message(FATAL_ERROR
        "Product generation failed during cross-target configuration; run the native generator "
        "directly for a redacted diagnostic")
    endif()
    include("${generated_dir}/product_assets.cmake")
    foreach(metadata IN ITEMS
            PRODUCT_SLUG DISPLAY_NAME DESCRIPTION AUTHOR OUTPUT_BASENAME TITLE_ID PRODUCT_CODE
            TOKEN_FILE_ENVIRONMENT CREDENTIAL_EXPIRY_POLICY ADAPTER ASSETS ADAPTER_SOURCES)
      set("OFFICE_3DS_GENERATED_${metadata}"
          "${OFFICE_3DS_GENERATED_${metadata}}" PARENT_SCOPE)
    endforeach()
  endif()

  file(GLOB_RECURSE product_dependencies CONFIGURE_DEPENDS
    "${product_dir}/*")
  add_custom_command(
    OUTPUT ${generated_outputs}
    COMMAND "${codegen_command}" ${command_args}
    DEPENDS ${product_dependencies} ${value_dependency} ${codegen_dependency}
    COMMENT "Generating product sources"
    VERBATIM
  )
  string(REPLACE "-" "_" target_suffix "${PRODUCT_SLUG}")
  add_custom_target(office_3ds_generate_product_${target_suffix} DEPENDS ${generated_outputs})
  if(NOT TARGET office_3ds_generate_product)
    add_custom_target(office_3ds_generate_product)
  endif()
  add_dependencies(office_3ds_generate_product office_3ds_generate_product_${target_suffix})

  file(GLOB_RECURSE product_adapter_sources CONFIGURE_DEPENDS
    "${product_dir}/src/*.cpp")
  add_library(office_3ds_product_${target_suffix} STATIC
    "${generated_dir}/product_config.cpp"
    "${generated_dir}/product_requests.cpp"
    "${generated_dir}/product_mapper.cpp"
    "${generated_dir}/product_adapter_factory.cpp"
    ${product_adapter_sources}
  )
  add_dependencies(office_3ds_product_${target_suffix}
    office_3ds_generate_product_${target_suffix})
  target_include_directories(office_3ds_product_${target_suffix} PUBLIC
    "${generated_dir}"
    "${product_dir}/src")
  target_link_libraries(office_3ds_product_${target_suffix} PUBLIC office3ds::api)
  target_link_libraries(office_3ds_product_${target_suffix} PUBLIC
    office3ds::generated_adapter)
  target_compile_features(office_3ds_product_${target_suffix} PUBLIC cxx_std_17)

  set(OFFICE_3DS_GENERATED_PRODUCT_ROOT "${generated_dir}" PARENT_SCOPE)
  set(OFFICE_3DS_PRODUCT_DIR "${product_dir}" PARENT_SCOPE)
  set(OFFICE_3DS_PRODUCT_TARGET "office_3ds_product_${target_suffix}" PARENT_SCOPE)
endfunction()
