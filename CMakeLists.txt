# pico-bcon firmware (RP2350 / pico2_w).
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
  project(pico-bcon C CXX ASM)
  pico_sdk_init()

  # Task 2: dual-core feasibility PoC.
  # Core0 = BTstack Classic+BLE + TinyUSB + CYW43 (wakecon同lib・同初期化順)。
  # Core1 = UART1 DMA + v3 parser + mutex push のみ。
  # POC_DATA_BAUD: UART1 baud (既定1000000=spec通り)。115200上限アダプタの
  # derated試験時は -DPOC_DATA_BAUD=115200 で構成する (build/はgit管理外)。
  set(POC_DATA_BAUD 1000000 CACHE STRING "PoC UART1 baud rate")
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
else()
  project(pico-bcon-hostonly C)
  message(STATUS "pico_sdk_import.cmake not found: firmware target skipped (host tests only).")
endif()
