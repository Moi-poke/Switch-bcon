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

add_executable(test_usb
  test_usb.c
  ../../src/proto/protocol.c
  ../../src/proto/pack.c
  ../../src/proto/spi.c
  ../../src/usb/usb_hid.c
)
target_include_directories(test_usb PRIVATE
  ../../src/proto
  ../../src/usb
)

add_executable(test_config
  test_config.c
  ../../src/proto/protocol.c
  ../../src/proto/dispatch.c
)
target_include_directories(test_config PRIVATE ../../src/proto)

add_executable(test_baud
  test_baud.c
  ../../src/proto/baud.c
)
target_include_directories(test_baud PRIVATE ../../src/proto)

enable_testing()
add_test(NAME protocol COMMAND test_protocol)
add_test(NAME usb COMMAND test_usb)
add_test(NAME config COMMAND test_config)
add_test(NAME baud COMMAND test_baud)
