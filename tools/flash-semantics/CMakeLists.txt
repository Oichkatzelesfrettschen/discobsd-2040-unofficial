cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Target board")

include(pico_sdk_import.cmake)

project(flash_semantics C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(flash_semantics main.c)

# no_flash links the image to run entirely from SRAM: the probe's own code
# never lives in the flash it measures, and only its one scratch sector is
# written.
pico_set_binary_type(flash_semantics no_flash)

target_link_libraries(flash_semantics pico_stdlib hardware_flash hardware_watchdog)

pico_enable_stdio_usb(flash_semantics 1)
pico_enable_stdio_uart(flash_semantics 0)

pico_add_extra_outputs(flash_semantics)
