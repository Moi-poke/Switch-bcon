# pico-bcon firmware (RP2350 / pico2_w). Full firmware target is added
# once the Pico SDK import file is present. Host tests always build.
cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PICO_BOARD pico2_w CACHE STRING "Board type")

if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)
  include(pico_sdk_import.cmake)
  project(pico-bcon C CXX ASM)
  pico_sdk_init()
  # Firmware sources land here as they are ported:
  #   src/main.c src/proto/protocol.c src/usb/... src/bt/...
  # (None yet -- protocol layer + tests first.)
else()
  project(pico-bcon-hostonly C)
  message(STATUS "pico_sdk_import.cmake not found: firmware target skipped (host tests only).")
endif()
