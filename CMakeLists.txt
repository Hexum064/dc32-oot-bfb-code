# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)



# initalize pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
#set(PICO_SDK_PATH "/home/carlk/pi/pico/pico-sdk")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)
include(pico_extras_import.cmake)

project(oot-bfb-rp2040 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# add_subdirectory(src)



# Add executable. Default name is the project name, version 0.1
add_executable(oot-bfb-rp2040)

pico_generate_pio_header(oot-bfb-rp2040 ${CMAKE_CURRENT_LIST_DIR}/src/i2s_32_441.pio)
pico_generate_pio_header(oot-bfb-rp2040 ${CMAKE_CURRENT_LIST_DIR}/src/ws2812b.pio)

target_sources(oot-bfb-rp2040 PUBLIC
    ./src/main.c
    ./src/data.S
    ./src/rand.c
    ./src/rand.h
)

target_link_libraries(oot-bfb-rp2040
    pico_stdio_usb 
    pico_stdlib 
    hardware_spi
    pico_audio_i2s
    pico_multicore
    hardware_pio
    hardware_adc
    hardware_pwm
)

pico_set_program_name(oot-bfb-rp2040 "oot-bfb-rp2040")
pico_set_program_version(oot-bfb-rp2040 "0.1")

# Choose source and destination for standard input and output:
#   See 4.1. Serial input and output on Raspberry Pi Pico in Getting started with Raspberry Pi Pico (https://datasheets.raspberrypi.org/pico/getting-started-with-pico.pdf)
#   and 2.7.1. Standard Input/Output (stdio) Support in Raspberry Pi Pico C/C++ SDK (https://datasheets.raspberrypi.org/pico/raspberry-pi-pico-c-sdk.pdf):
pico_enable_stdio_uart(oot-bfb-rp2040 0)
pico_enable_stdio_usb(oot-bfb-rp2040 1)

pico_add_extra_outputs(oot-bfb-rp2040)

