# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.13)

# ボードの指定。pico_sdk_import.cmake より前に決めておく必要がある。
# 未指定だと SDK は Raspberry Pi Pico 扱いにし、XIAO RP2040 に必要な水晶の起動待ち延長などが入らない。
if(NOT DEFINED PICO_BOARD AND NOT DEFINED ENV{PICO_BOARD})
  set(PICO_BOARD seeed_xiao_rp2040 CACHE STRING "Board type")
endif()

include(pico_sdk_import.cmake)

project(xiao_takeover_ctrl C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

pico_sdk_init()

option(TAKEOVER_ENABLE_WATCHDOG "Enable the watchdog (reset when the main loop stalls)" ON)

set(TAKEOVER_SOURCES
  src/main.c
  src/takeover_logic.c
  src/button_input.c
  src/outputs.c
  src/panic_hook.c
)

add_executable(xiao_takeover_ctrl ${TAKEOVER_SOURCES})

# src/debounce.pio を pioasm でアセンブルし、debounce.pio.h を生成する
pico_generate_pio_header(xiao_takeover_ctrl ${CMAKE_CURRENT_LIST_DIR}/src/debounce.pio)

target_include_directories(xiao_takeover_ctrl PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)

if(TAKEOVER_ENABLE_WATCHDOG)
  target_compile_definitions(xiao_takeover_ctrl PRIVATE TAKEOVER_ENABLE_WATCHDOG=1)
else()
  target_compile_definitions(xiao_takeover_ctrl PRIVATE TAKEOVER_ENABLE_WATCHDOG=0)
endif()

# SDK の panic() を takeover_panic() に置き換える(緑 LED を消して停止する。仕組みは include/panic_hook.h を参照)。
# PRIVATE の定義は、この実行ファイルに組み込まれる SDK のソース(pico_platform_panic の panic.c /
# custom_panic_function.S)にも適用されるので、SDK 側の panic がこの関数へ転送されるようになる
target_compile_definitions(xiao_takeover_ctrl PRIVATE
  PICO_PANIC_FUNCTION=takeover_panic
  PICO_PANIC_FUNCTION_DOES_NOT_RETURN=1
)

# 警告は自分のソースにだけ付ける(target_compile_options だと SDK のソースにも付いてしまう)
set_source_files_properties(${TAKEOVER_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

target_link_libraries(xiao_takeover_ctrl
  pico_stdlib
  hardware_pio
  hardware_clocks
  hardware_watchdog
)

# printf は使わないので stdio の出力先は両方無効にする
pico_enable_stdio_usb(xiao_takeover_ctrl 0)
pico_enable_stdio_uart(xiao_takeover_ctrl 0)

# .uf2 / .bin / .hex / .dis / .map を生成する
pico_add_extra_outputs(xiao_takeover_ctrl)
