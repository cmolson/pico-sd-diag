// SPDX-License-Identifier: CC0-1.0
// Character ROM handling, pruned from the EconoPET firmware (no FPGA paths).

#include "pch.h"
#include "roms.h"

#include "system_state.h"

// Commodore 901447-10 character generator ROM (2KB).
const uint8_t __in_flash(".rom_chars_e800") rom_chars_e800[] = {
    #include "font_901447_10.h"
};

const uint8_t* const p_video_font_000 = rom_chars_e800;

#define CHAR_ROM_SRAM_SIZE 4096

static uint8_t custom_char_rom[CHAR_ROM_SRAM_SIZE];

// The DVI core renders from custom_char_rom; zeros = black screen.
void roms_use_builtin_char_rom(void) {
    memcpy(custom_char_rom, rom_chars_e800, sizeof(rom_chars_e800));
}

const uint8_t* roms_get_char_rom(bool video_graphics) {
    // Quadrant {MA13 (R12 bit 5), video_graphics}.
    const bool crtc_chr_option =
        (system_state.pet_crtc_registers[CRTC_R12_START_ADDR_HI] & 0x20) != 0;
    const unsigned int quadrant = ((unsigned int) crtc_chr_option << 1) | (video_graphics ? 1u : 0u);
    return custom_char_rom + quadrant * 0x400;
}
