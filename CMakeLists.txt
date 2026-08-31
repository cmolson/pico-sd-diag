cmake_minimum_required(VERSION 3.13)

# Must include the Pico SDK before 'project()'
include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
set(PICO_SDK_REQUIRED_VERSION "2.1.1")
if (PICO_SDK_VERSION_STRING VERSION_LESS PICO_SDK_REQUIRED_VERSION)
    message(FATAL_ERROR "Raspberry Pi Pico SDK version ${PICO_SDK_REQUIRED_VERSION} (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

project(pico_sd_diag
    VERSION 0.1.0
    LANGUAGES C CXX ASM)

pico_sdk_init()

add_compile_options(-Wall -Wextra -Wnull-dereference)

# External libraries (vendored copies; see README)
add_subdirectory(external/PicoDVI/software/libdvi)
add_subdirectory(external/pico-vfs)

add_executable(pico_sd_diag
    src/main.c
    src/sd_diag.c
    src/sd_diag_usb.c
    src/roms/roms.c
    src/system_state.c
    src/display/char_encoding.c
    src/display/window.c
    src/display/dvi/dvi.c
    src/display/dvi/tmds_encode.c
    src/display/dvi/tmds_encode.S
)

# Expose tusb_config.h / pch.h / board.h to all sources
target_include_directories(pico_sd_diag PRIVATE
    src
    external/PicoDVI/software/include)

target_compile_definitions(pico_sd_diag PRIVATE
    # Always build the USB CDC serial console
    SD_DIAG_USB_CDC=1

    # DVI_VERTICAL_REPEAT=2 halves the vertical resolution to 240p by displaying
    # each scanline twice, which buys more time to prepare each frame.
    # DVI_N_TMDS_BUFFERS=4: one buffer is permanently reserved for a
    # precalculated blank scanline (see 'blank_tmdsbuf' in dvi.c).
    DVI_VERTICAL_REPEAT=2
    DVI_N_TMDS_BUFFERS=4

    # TMDS LUT lives in SRAM4; core 1 needs very little stack.
    PICO_CORE1_STACK_SIZE=0x200)

target_link_libraries(pico_sd_diag PRIVATE
    hardware_dma
    hardware_pio
    hardware_pwm
    hardware_spi
    hardware_vreg
    libdvi
    pico_multicore
    pico_stdlib
    pico_util
    tinyusb_device
    tinyusb_board

    # pico-vfs
    blockdevice_sd
    filesystem_fat
    filesystem_vfs
)

# create map/bin/hex/uf2 files
pico_add_extra_outputs(pico_sd_diag)
