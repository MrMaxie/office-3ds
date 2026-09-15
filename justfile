configure:
  cmake --preset host-debug

build: configure
  cmake --build --preset host-debug

test: build
  ctest --preset host-debug

generate-demo: build
  cmake --build build/host-debug --target office_3ds_generate_product

build-host-tools:
  cmake --preset host-tools
  cmake --build --preset host-tools

configure-3ds: build-host-tools
  cmake --preset 3ds-debug

build-3ds: configure-3ds
  cmake --build --preset 3ds-debug

build-3ds-release: build-host-tools
  cmake --preset 3ds-release
  cmake --build --preset 3ds-release

format:
  cmake -DOFFICE_3DS_FORMAT_MODE=write -P cmake/format.cmake

format-check:
  cmake -DOFFICE_3DS_FORMAT_MODE=check -P cmake/format.cmake

lint: build
  cppcheck --project=build/host-debug/compile_commands.json -ibuild/host-debug/_deps --enable=warning,style,performance,portability --error-exitcode=1 --suppress=missingIncludeSystem --suppress=assertWithSideEffect --suppress=useStlAlgorithm --suppress=unusedStructMember --suppress=preprocessorErrorDirective --inline-suppr
