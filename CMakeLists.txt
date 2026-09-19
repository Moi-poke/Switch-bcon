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
    src/usb/usb_wired.c
    src/bt/hid.c
    src/bt/link.c
    src/bt/link_conn.c
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
  )

  pico_add_extra_outputs(switch-bcon)
else()
  project(switch-bcon-hostonly C)
  message(STATUS "pico_sdk_import.cmake not found: firmware target skipped (host tests only).")
endif()
