cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Target board")

include(pico_sdk_import.cmake)

project(flash_id C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(flash_id main.c)

# no_flash links the image to run entirely from SRAM, so loading and running the
# probe leaves the flash contents as they were.
pico_set_binary_type(flash_id no_flash)

target_link_libraries(flash_id pico_stdlib hardware_flash)

pico_enable_stdio_usb(flash_id 1)
pico_enable_stdio_uart(flash_id 0)

pico_add_extra_outputs(flash_id)
