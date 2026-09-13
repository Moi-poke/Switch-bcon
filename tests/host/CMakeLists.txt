# tests/host/CMakeLists.txt -- host unit tests (MSVC via vcvars64).
# Run from a cmd with vcvars64.bat already called:
#   cmake -S tests/host -B build-host
#   cmake --build build-host --config Debug
#   ctest --test-dir build-host -V
cmake_minimum_required(VERSION 3.16)
project(pico_bcon_host_tests C)

set(CMAKE_C_STANDARD 11)

# MSVC C4819 (Japanese comments)対策
if(MSVC)
  add_compile_options(/utf-8 /W3)
else()
  add_compile_options(-Wall -Wextra -O2)
endif()

add_executable(test_protocol
  test_protocol.c
  ../../src/proto/protocol.c
)
target_include_directories(test_protocol PRIVATE ../../src/proto)

enable_testing()
add_test(NAME protocol COMMAND test_protocol)
