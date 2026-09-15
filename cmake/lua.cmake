include(FetchContent)

if(TARGET office_3ds_lua)
  return()
endif()

FetchContent_Declare(
  office_3ds_lua_source
  URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
  URL_HASH SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(office_3ds_lua_source)

set(office_3ds_lua_sources
  lapi.c
  lauxlib.c
  lbaselib.c
  lcode.c
  lcorolib.c
  lctype.c
  ldblib.c
  ldebug.c
  ldo.c
  ldump.c
  lfunc.c
  lgc.c
  linit.c
  liolib.c
  llex.c
  lmathlib.c
  lmem.c
  loadlib.c
  lobject.c
  lopcodes.c
  loslib.c
  lparser.c
  lstate.c
  lstring.c
  lstrlib.c
  ltable.c
  ltablib.c
  ltm.c
  lundump.c
  lutf8lib.c
  lvm.c
  lzio.c
)
list(TRANSFORM office_3ds_lua_sources PREPEND "${office_3ds_lua_source_SOURCE_DIR}/src/")

add_library(office_3ds_lua STATIC ${office_3ds_lua_sources})
target_include_directories(office_3ds_lua PUBLIC "${office_3ds_lua_source_SOURCE_DIR}/src")
target_compile_definitions(office_3ds_lua PRIVATE LUA_USE_C89)
set_target_properties(office_3ds_lua PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
