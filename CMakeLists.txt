# switch-bcon firmware (RP2350 / pico2_w).
# With Pico SDK: firmware targets (poc_dualcore, ...). Without: host-only.
cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PICO_BOARD pico2_w CACHE STRING "Board type")

if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if(EXISTS ${picoVscode})
    include(${picoVscode})
endif()

if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)
  include(pico_sdk_import.cmake)
  project(switch-bcon C CXX ASM)
  pico_sdk_init()

  # Task 2: dual-core feasibility PoC.
  # Core0 = BTstack Classic+BLE + TinyUSB + CYW43 (wakecon同lib・同初期化順)。
  # Core1 = UART1 DMA + v3 parser + mutex push のみ。
  # POC_DATA_BAUD: UART1 baud (既定1000000=spec通り)。115200上限アダプタの
  # derated試験時は -DPOC_DATA_BAUD=115200 で構成する (build/はgit管理外)。
  set(POC_DATA_BAUD 1000000 CACHE STRING "PoC UART1 baud rate")
  # WIRED_DEFAULT: TLV未保存時の起動モード (1=有線既定)。
  # 無線起動の試験時は -DWIRED_DEFAULT=0 で別構成する。
  set(WIRED_DEFAULT 1 CACHE STRING "Default boot mode when TLV unwritten (1=wired)")
  # POKECON_INPUT: Core1の入力方言 (0=v3バイナリ既定・1=PokeCon Modified行)。
  # 自動判別なし。PokeCon側は115200のため -DPOC_DATA_BAUD=115200 と組で使う。
  set(POKECON_INPUT 0 CACHE STRING "Core1 input dialect (0=v3 binary, 1=pokecon lines)")
  # IMU_DUMMY: Phase 0 実験ビルド (0=現行既定・1=ダミーIMU)。
  # ON時のみ実験パラメータをcompile definitionへ渡し、imu_dummy.cをlinkする。
  set(IMU_DUMMY 0 CACHE STRING "Phase 0 dummy IMU experiment (0=off, 1=on)")
  # JOY_SPI_CAL_ROT: role2 (Joy-Con R) の SPI 校正値(0x6020/0x6080)の回転。
  #   0=回さない / 1=Y,Z反転 (既定・実機動作確認済み) / 4=X,Z反転
  # 0x30 IMU 本体の軸写像は実機測定で確定済み (src/usb/usb_hid.c
  # k_joy_negate_axis)。ここは校正値側のA/B専用。
  set(JOY_SPI_CAL_ROT 1 CACHE STRING "role2 SPI cal rotation (0=none, 1=YZ, 4=XZ)")
  # JOY_USB_PID_2007: 診断用。role2のUSB PIDを0x2007にしてWebHID等に
  # Joy-Con R として認識させる（既定は全role 0x2009）。
  set(JOY_USB_PID_2007 0 CACHE STRING "role2 USB PID 0x2007 for WebHID tests")
  set(IMU_DUMMY_INTERVAL_MS 8 CACHE STRING "Dummy USB report interval ms (ON only)")
  set(IMU_DUMMY_MODE 1 CACHE STRING "Dummy mode (ON only: 0=SINE, 1=STEP)")
  set(IMU_DUMMY_GYRO_AXIS 2 CACHE STRING "Dummy gyro axis 0..2 (ON only)")
  set(IMU_DUMMY_G_RAW 5899 CACHE STRING "Dummy STEP raw int16 (ON only)")
  set(IMU_DUMMY_T_MS 4000 CACHE STRING "Dummy STEP duration ms (ON only)")
  set(IMU_DUMMY_SINE_AMP_RAW 1475 CACHE STRING "Dummy SINE amplitude raw (ON only)")
  set(IMU_DUMMY_SINE_PERIOD_MS 2000 CACHE STRING "Dummy SINE period ms (ON only)")
  set(IMU_DUMMY_FILL_MASK 7 CACHE STRING "Dummy fill mask bit0=slot0 (ON only)")
  set(IMU_DUMMY_ACC_X 0 CACHE STRING "Dummy static accel X int16 (ON only)")
  set(IMU_DUMMY_ACC_Y 0 CACHE STRING "Dummy static accel Y int16 (ON only)")
  set(IMU_DUMMY_ACC_Z 4096 CACHE STRING "Dummy static accel Z int16 (ON only)")
  set(IMU_DUMMY_FORCE 0 CACHE STRING "Dummy force-active (ON only: 0/1)")
  set(IMU_DUMMY_STEP_AUTOREPEAT_MS 0 CACHE STRING "Dummy STEP autorepeat ms, 0=manual (ON only)")
  set(IMU_DUMMY_DIAG 0 CACHE STRING "Dummy diag logs (§1, ON only: 0/1)")
  # usb_cdc.c は常時リンク (HID profile時はconnected=falseで不応答)。
  add_executable(poc_dualcore
    src/poc_dualcore/main.c
    src/poc_dualcore/poc_usb.c
    src/proto/protocol.c
  )

  pico_set_program_name(poc_dualcore "poc-dualcore")
  pico_set_program_version(poc_dualcore "0.1")

  # ログは UART0 (GP0/1)。USB は PoC stub が専有、UART1 は data path。
  pico_enable_stdio_uart(poc_dualcore 1)
  pico_enable_stdio_usb(poc_dualcore 0)

  target_include_directories(poc_dualcore PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/poc_dualcore
    ${CMAKE_CURRENT_LIST_DIR}/src/proto
  )

  target_link_libraries(poc_dualcore
    pico_stdlib
    hardware_uart
    hardware_dma
    hardware_flash
    pico_flash
    pico_multicore
    pico_unique_id
    pico_btstack_classic
    pico_btstack_ble
    pico_btstack_cyw43
    pico_cyw43_arch_none
    pico_btstack_flash_bank
    tinyusb_device
    tinyusb_board
  )

  # 参考 pico-examples usb/device/dev_hid_composite: TinyUSB を Pico 常駐で使う。
  target_compile_definitions(poc_dualcore PRIVATE
    CFG_TUSB_OS=OPT_OS_PICO
    POC_DATA_BAUD=${POC_DATA_BAUD}
  )

  pico_add_extra_outputs(poc_dualcore)

  # Task 4: 統合FW (USB + Classic BT + BLE wake。無線はWIRED_MODE管理)。
  # Core0 = BTstack run loop＋TinyUSB＋CYW43 (wakecon同lib・同初期化順)。
  # Core1 = UART1 DMA＋v3 parser＋mutex push。
  add_executable(switch-bcon
    src/main.c
    src/pokecon.c
    src/proto/protocol.c
    src/proto/baud.c
    src/proto/pack.c
    src/proto/spi.c
    src/proto/dispatch.c
    src/usb/usb_descriptors.c
    src/usb/usb_hid.c
    src/usb/usb_cdc.c
    src/usb/usb_wired.c
    src/bt/hid.c
    src/bt/hid_pack.c
    src/bt/personality.c
    src/bt/link.c
    src/bt/link_conn.c
    src/bt/reconnect_policy.c
    src/bt/link_cap.c
    src/bt/link_beacon.c
    src/bt/cap.c
    src/bt/store.c
  )

  pico_set_program_name(switch-bcon "switch-bcon")
  pico_set_program_version(switch-bcon "0.1")

  # ログは UART0 (GP0/1)。USB は有線ProConが専有、UART1 は data path。
  pico_enable_stdio_uart(switch-bcon 1)
  pico_enable_stdio_usb(switch-bcon 0)

  target_include_directories(switch-bcon PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/usb
    ${CMAKE_CURRENT_LIST_DIR}/src/proto
    ${CMAKE_CURRENT_LIST_DIR}/src/bt
  )

  target_link_libraries(switch-bcon
    pico_stdlib
    hardware_uart
    hardware_dma
    hardware_flash
    hardware_watchdog
    pico_flash
    pico_multicore
    pico_unique_id
    pico_btstack_classic
    pico_btstack_ble
    pico_btstack_cyw43
    pico_cyw43_arch_none
    pico_btstack_flash_bank
    tinyusb_device
    tinyusb_board
  )

  target_compile_definitions(switch-bcon PRIVATE
    CFG_TUSB_OS=OPT_OS_PICO
    POC_DATA_BAUD=${POC_DATA_BAUD}
    WIRED_DEFAULT=${WIRED_DEFAULT}
    POKECON_INPUT=${POKECON_INPUT}
    IMU_DUMMY=${IMU_DUMMY}
    JOY_SPI_CAL_ROT=${JOY_SPI_CAL_ROT}
    JOY_USB_PID_2007=${JOY_USB_PID_2007}
  )

  # Phase 0: ON時のみ実験パラメータとimu_dummy.cを足す。libmはSINE用。
  if(IMU_DUMMY)
    # 由来追跡: IMUD build行へ短縮hashとdirty flagを載せる (ON時のみ)。
    execute_process(COMMAND git rev-parse --short HEAD
      WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
      OUTPUT_VARIABLE IMU_DUMMY_GIT_HASH_V OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    execute_process(COMMAND git status --porcelain
      WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
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
    target_sources(switch-bcon PRIVATE src/usb/imu_dummy.c)
    target_compile_definitions(switch-bcon PRIVATE
      IMU_DUMMY_INTERVAL_MS=${IMU_DUMMY_INTERVAL_MS}
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
      IMU_DUMMY_DIAG=${IMU_DUMMY_DIAG}
      IMU_DUMMY_GIT_HASH="${IMU_DUMMY_GIT_HASH_V}"
      IMU_DUMMY_GIT_DIRTY="${IMU_DUMMY_GIT_DIRTY_V}"
    )
    target_link_libraries(switch-bcon m)
  endif()

  pico_add_extra_outputs(switch-bcon)

  # gyro-mini: ジャイロ再現の最小FW (USB有線ProCon + 0x30 + IMU波形のみ)。
  # 本線 usb_* / pack / spi / imu_dummy を同一ソースでリンクする。
  # timer 5ms刻み・周期15ms・静止1gは本線へ移植済み (2026-10-17)。
  # mini固有の差分は B自動押下 (main_mini.c) のみ。
  # 構成は -DMINI_GYRO_INTERVAL_MS= / -DMINI_GYRO_AXIS= / -DMINI_GYRO_MODE= で
  # 変える (IMU_DUMMY_* へ転写する)。既定は SINE/軸2(Z)/15ms。
  set(MINI_GYRO 0 CACHE STRING "gyro-mini minimal gyro FW (0=off, 1=on)")
  set(MINI_GYRO_INTERVAL_MS 15 CACHE STRING "mini 0x30 interval ms")
  set(MINI_GYRO_MODE 0 CACHE STRING "mini dummy mode (0=SINE, 1=STEP)")
  set(MINI_GYRO_AXIS 2 CACHE STRING "mini gyro axis 0..2")
  set(MINI_GYRO_G_RAW 5899 CACHE STRING "mini STEP raw int16")
  set(MINI_GYRO_SINE_AMP_RAW 4096 CACHE STRING "mini SINE amplitude raw")
  set(MINI_GYRO_SINE_PERIOD_MS 2000 CACHE STRING "mini SINE period ms")
  set(MINI_GYRO_ACC_Z 4096 CACHE STRING "mini static accel Z int16")
  set(MINI_GYRO_DIAG 1 CACHE STRING "mini diag logs (0/1)")
  if(MINI_GYRO)
    add_executable(gyro-mini
      src/mini/main_mini.c
      src/usb/usb_descriptors.c
      src/usb/usb_hid.c
      src/usb/usb_wired.c
      src/usb/usb_cdc.c
      src/usb/imu_dummy.c
      src/proto/protocol.c
      src/proto/pack.c
      src/proto/spi.c
      src/proto/dispatch.c
    )
    pico_set_program_name(gyro-mini "gyro-mini")
    pico_set_program_version(gyro-mini "0.1")
    pico_enable_stdio_uart(gyro-mini 1)
    pico_enable_stdio_usb(gyro-mini 0)
    target_include_directories(gyro-mini PRIVATE
      ${CMAKE_CURRENT_LIST_DIR}/src
      ${CMAKE_CURRENT_LIST_DIR}/src/usb
      ${CMAKE_CURRENT_LIST_DIR}/src/proto
      ${CMAKE_CURRENT_LIST_DIR}/src/mini
    )
    target_link_libraries(gyro-mini
      pico_stdlib
      hardware_uart
      pico_unique_id
      tinyusb_device
      tinyusb_board
      m
    )
    target_compile_definitions(gyro-mini PRIVATE
      CFG_TUSB_OS=OPT_OS_PICO
      IMU_DUMMY=1
      IMU_DUMMY_INTERVAL_MS=${MINI_GYRO_INTERVAL_MS}
      IMU_DUMMY_MODE=${MINI_GYRO_MODE}
      IMU_DUMMY_GYRO_AXIS=${MINI_GYRO_AXIS}
      IMU_DUMMY_G_RAW=${MINI_GYRO_G_RAW}
      IMU_DUMMY_T_MS=4000
      IMU_DUMMY_SINE_AMP_RAW=${MINI_GYRO_SINE_AMP_RAW}
      IMU_DUMMY_SINE_PERIOD_MS=${MINI_GYRO_SINE_PERIOD_MS}
      IMU_DUMMY_FILL_MASK=7
      IMU_DUMMY_ACC_X=0
      IMU_DUMMY_ACC_Y=0
      IMU_DUMMY_ACC_Z=${MINI_GYRO_ACC_Z}
      IMU_DUMMY_FORCE=0
      IMU_DUMMY_STEP_AUTOREPEAT_MS=0
      IMU_DUMMY_DIAG=${MINI_GYRO_DIAG}
      IMU_DUMMY_GIT_HASH="mini"
      IMU_DUMMY_GIT_DIRTY="0"
    )
    pico_add_extra_outputs(gyro-mini)
  endif()
else()
  project(switch-bcon-hostonly C)
  message(STATUS "pico_sdk_import.cmake not found: firmware target skipped (host tests only).")
endif()
