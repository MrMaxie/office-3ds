if(NOT DEFINED OFFICE_3DS_RUNTIME_BINARY OR
   NOT EXISTS "${OFFICE_3DS_RUNTIME_BINARY}")
  message(FATAL_ERROR "OFFICE_3DS_RUNTIME_BINARY must name a built runtime artifact")
endif()

file(STRINGS "${OFFICE_3DS_RUNTIME_BINARY}" forbidden_strings
  REGEX "product\\.lua|luaL_|Lua 5\\.4" LIMIT_COUNT 1)
if(forbidden_strings)
  message(FATAL_ERROR
    "Runtime artifact contains a forbidden Lua or product manifest marker")
endif()
