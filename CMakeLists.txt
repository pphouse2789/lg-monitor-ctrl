cmake_minimum_required(VERSION 3.13)

# --- Pico SDK の読み込み ---
# 環境変数 PICO_SDK_PATH にpico-sdkのパスを設定してください
# 例: export PICO_SDK_PATH=~/pico/pico-sdk
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(picossci2_kbd_uart C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Picossci 2 Tiny は RP2350A ベース（Pico 2 互換）
set(PICO_BOARD pico2)

pico_sdk_init()

add_executable(picossci2_kbd_uart
    main.c
)

target_include_directories(picossci2_kbd_uart PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(picossci2_kbd_uart
    pico_stdlib
    pico_multicore
    tinyusb_host
    tinyusb_board
)

# USB は Host モードで使うため、USB stdio は無効化
# デバッグ出力は UART (GPIO0=TX, GPIO1=RX) 経由
pico_enable_stdio_usb(picossci2_kbd_uart 0)
pico_enable_stdio_uart(picossci2_kbd_uart 1)

# .uf2 ファイルなどの追加出力を生成
pico_add_extra_outputs(picossci2_kbd_uart)
