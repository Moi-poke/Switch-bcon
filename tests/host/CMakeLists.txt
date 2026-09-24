# tests/host/CMakeLists.txt -- host unit tests (MSVC via vcvars64).
# Run from a cmd with vcvars64.bat already called:
#   cmake -S tests/host -B build-host
#   cmake --build build-host --config Debug
#   ctest --test-dir build-host -C Debug -V
cmake_minimum_required(VERSION 3.16)
project(switch_bcon_host_tests C)

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
  ../../src/proto/dispatch.c
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

add_executable(test_pokecon
  test_pokecon.c
  ../../src/pokecon.c
)
target_include_directories(test_pokecon PRIVATE
  ../../src
  ../../src/proto
)

add_executable(test_rumble
  test_rumble.c
)
target_include_directories(test_rumble PRIVATE ../../src/proto)

add_executable(test_personality
  test_personality.c
  ../../src/proto/protocol.c
  ../../src/proto/pack.c
  ../../src/proto/dispatch.c
  ../../src/bt/personality.c
  ../../src/usb/usb_hid.c
)
target_include_directories(test_personality PRIVATE
  ../../src/proto
  ../../src/bt
  ../../src/usb
)

add_executable(test_joy_scopeA
  test_joy_scopeA.c
  ../../src/proto/protocol.c
  ../../src/proto/pack.c
  ../../src/proto/dispatch.c
  ../../src/bt/personality.c
  ../../src/usb/usb_hid.c
)
target_include_directories(test_joy_scopeA PRIVATE
  ../../src/proto
  ../../src/bt
  ../../src/usb
)

add_executable(test_joy_scopeB
  test_joy_scopeB.c
  ../../src/proto/protocol.c
  ../../src/proto/pack.c
  ../../src/proto/dispatch.c
  ../../src/bt/personality.c
  ../../src/usb/usb_hid.c
)
target_include_directories(test_joy_scopeB PRIVATE
  ../../src/proto
  ../../src/bt
  ../../src/usb
)

add_executable(test_wired_player
  test_wired_player.c
  ../../src/proto/protocol.c
  ../../src/proto/pack.c
  ../../src/proto/spi.c
  ../../src/proto/dispatch.c
  ../../src/usb/usb_hid.c
)
target_include_directories(test_wired_player PRIVATE
  ../../src/proto
  ../../src/usb
)
# 有線プレイヤーランプ配管 (save/get/feed は src/usb/usb_hid.c が提供)。

add_executable(test_player_capsaved
  test_player_capsaved.c
  ../../src/proto/protocol.c
  ../../src/proto/dispatch.c
)
target_include_directories(test_player_capsaved PRIVATE ../../src/proto)

enable_testing()
add_test(NAME protocol COMMAND test_protocol)
add_test(NAME usb COMMAND test_usb)
add_test(NAME config COMMAND test_config)
add_test(NAME baud COMMAND test_baud)
add_test(NAME pokecon COMMAND test_pokecon)
add_test(NAME rumble COMMAND test_rumble)
add_test(NAME personality COMMAND test_personality)
add_test(NAME joy_scopeA COMMAND test_joy_scopeA)
add_test(NAME joy_scopeB COMMAND test_joy_scopeB)
add_test(NAME wired_player COMMAND test_wired_player)
add_test(NAME player_capsaved COMMAND test_player_capsaved)
