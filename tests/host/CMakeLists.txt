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

# role2 のIMU反転モードを 1 (既定) と 2 (ジャイロのみ) の両方で検証する。
# 同じソースを JOY_IMU_NEG_MODE 付きでビルドし、期待値を切り替える。
foreach(neg_mode 1 2)
  add_executable(test_joy_imu_neg_m${neg_mode}
    test_joy_imu_neg.c
    ../../src/proto/protocol.c
    ../../src/proto/pack.c
    ../../src/proto/spi.c
    ../../src/proto/dispatch.c
    ../../src/usb/usb_hid.c
  )
  target_include_directories(test_joy_imu_neg_m${neg_mode} PRIVATE
    ../../src/proto
    ../../src/usb
  )
  target_compile_definitions(test_joy_imu_neg_m${neg_mode} PRIVATE
    JOY_IMU_NEG_MODE=${neg_mode}
  )
  add_test(NAME joy_imu_neg_m${neg_mode} COMMAND test_joy_imu_neg_m${neg_mode})
endforeach()

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

add_executable(test_color_getinfo
  test_color_getinfo.c
  ../../src/proto/protocol.c
  ../../src/proto/dispatch.c
  ../../src/proto/spi.c
)
target_include_directories(test_color_getinfo PRIVATE ../../src/proto)

add_executable(test_reconnect_policy
  test_reconnect_policy.c
  ../../src/bt/reconnect_policy.c
)
target_include_directories(test_reconnect_policy PRIVATE ../../src/bt)

add_executable(test_usb_cdc
  test_usb_cdc.c
  ../../src/proto/protocol.c
  ../../src/proto/dispatch.c
  ../../src/proto/baud.c
)
target_include_directories(test_usb_cdc PRIVATE ../../src/proto)
add_executable(test_virt_tick
  test_virt_tick.c
  ../../src/proto/protocol.c
  ../../src/proto/dispatch.c
  ../../src/proto/baud.c
)
target_include_directories(test_virt_tick PRIVATE ../../src/proto)

# Phase 0: IMU_DUMMY=ON構成のときだけ定義する。既存15件には触れない。
# 構成マトリクスは -DIMU_DUMMY_*= で切る (既定=A: MODE1/MASK7/FORCE0)。
option(IMU_DUMMY "Phase 0 dummy IMU host test" OFF)
set(IMU_DUMMY_MODE 1 CACHE STRING "Dummy mode (0=SINE, 1=STEP)")
set(IMU_DUMMY_GYRO_AXIS 2 CACHE STRING "Dummy gyro axis 0..2")
set(IMU_DUMMY_G_RAW 5899 CACHE STRING "Dummy STEP raw int16")
set(IMU_DUMMY_T_MS 4000 CACHE STRING "Dummy STEP duration ms")
set(IMU_DUMMY_SINE_AMP_RAW 1475 CACHE STRING "Dummy SINE amplitude raw")
set(IMU_DUMMY_SINE_PERIOD_MS 2000 CACHE STRING "Dummy SINE period ms")
set(IMU_DUMMY_FILL_MASK 7 CACHE STRING "Dummy fill mask bit0=slot0")
set(IMU_DUMMY_ACC_X 0 CACHE STRING "Dummy static accel X int16")
set(IMU_DUMMY_ACC_Y 0 CACHE STRING "Dummy static accel Y int16")
set(IMU_DUMMY_ACC_Z 4096 CACHE STRING "Dummy static accel Z int16")
set(IMU_DUMMY_FORCE 0 CACHE STRING "Dummy force-active 0/1")
set(IMU_DUMMY_STEP_AUTOREPEAT_MS 0 CACHE STRING "Dummy STEP autorepeat ms")
set(IMU_DUMMY_INTERVAL_MS 8 CACHE STRING "Dummy USB interval ms")
set(IMU_DUMMY_DIAG 0 CACHE STRING "Dummy diag logs (§1: 0/1)")
if(IMU_DUMMY)
  execute_process(COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../..
    OUTPUT_VARIABLE IMU_DUMMY_GIT_HASH_V OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  execute_process(COMMAND git status --porcelain
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../..
    OUTPUT_VARIABLE IMU_DUMMY_GIT_STATUS_V OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(IMU_DUMMY_GIT_HASH_V STREQUAL "")
    set(IMU_DUMMY_GIT_HASH_V "unknown")
  endif()
  if(IMU_DUMMY_GIT_STATUS_V STREQUAL "")
    set(IMU_DUMMY_GIT_DIRTY_V "0")
  else()
    set(IMU_DUMMY_GIT_DIRTY_V "1")
  endif()
  add_executable(test_imu_dummy
    test_imu_dummy.c
    ../../src/usb/imu_dummy.c
  )
  target_include_directories(test_imu_dummy PRIVATE ../../src/usb)
  target_compile_definitions(test_imu_dummy PRIVATE
    IMU_DUMMY=1
    IMU_DUMMY_MODE=${IMU_DUMMY_MODE}
    IMU_DUMMY_GYRO_AXIS=${IMU_DUMMY_GYRO_AXIS}
    IMU_DUMMY_G_RAW=${IMU_DUMMY_G_RAW}
    IMU_DUMMY_T_MS=${IMU_DUMMY_T_MS}
    IMU_DUMMY_SINE_AMP_RAW=${IMU_DUMMY_SINE_AMP_RAW}
    IMU_DUMMY_SINE_PERIOD_MS=${IMU_DUMMY_SINE_PERIOD_MS}
    IMU_DUMMY_FILL_MASK=${IMU_DUMMY_FILL_MASK}
    IMU_DUMMY_ACC_X=${IMU_DUMMY_ACC_X}
    IMU_DUMMY_ACC_Y=${IMU_DUMMY_ACC_Y}
    IMU_DUMMY_ACC_Z=${IMU_DUMMY_ACC_Z}
    IMU_DUMMY_FORCE=${IMU_DUMMY_FORCE}
    IMU_DUMMY_STEP_AUTOREPEAT_MS=${IMU_DUMMY_STEP_AUTOREPEAT_MS}
    IMU_DUMMY_INTERVAL_MS=${IMU_DUMMY_INTERVAL_MS}
    IMU_DUMMY_DIAG=${IMU_DUMMY_DIAG}
    IMU_DUMMY_GIT_HASH="${IMU_DUMMY_GIT_HASH_V}"
    IMU_DUMMY_GIT_DIRTY="${IMU_DUMMY_GIT_DIRTY_V}"
  )
  if(NOT MSVC)
    target_link_libraries(test_imu_dummy m)
  endif()
endif()
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
add_test(NAME color_getinfo COMMAND test_color_getinfo)
add_test(NAME reconnect_policy COMMAND test_reconnect_policy)
add_test(NAME usb_cdc COMMAND test_usb_cdc)
add_test(NAME virt_tick COMMAND test_virt_tick)
if(IMU_DUMMY)
  add_test(NAME imu_dummy COMMAND test_imu_dummy)
endif()
