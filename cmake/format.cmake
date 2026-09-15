if(NOT DEFINED OFFICE_3DS_FORMAT_MODE)
  set(OFFICE_3DS_FORMAT_MODE check)
endif()

find_program(OFFICE_3DS_CLANG_FORMAT clang-format REQUIRED)
file(GLOB_RECURSE office_3ds_sources
  LIST_DIRECTORIES FALSE
  "${CMAKE_CURRENT_LIST_DIR}/../include/*.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/*.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/*.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/../tests/*.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../tests/*.hpp"
)

foreach(source IN LISTS office_3ds_sources)
  if(OFFICE_3DS_FORMAT_MODE STREQUAL write)
    execute_process(
      COMMAND "${OFFICE_3DS_CLANG_FORMAT}" -i "${source}"
      COMMAND_ERROR_IS_FATAL ANY
    )
  else()
    execute_process(
      COMMAND "${OFFICE_3DS_CLANG_FORMAT}" --dry-run --Werror "${source}"
      COMMAND_ERROR_IS_FATAL ANY
    )
  endif()
endforeach()
