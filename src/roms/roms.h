// SPDX-License-Identifier: CC0-1.0
// Character ROM for the DVI renderer (pruned from the EconoPET firmware).

#pragma once

#include <stdbool.h>
#include <stdint.h>

extern const uint8_t rom_chars_e800[0x800];
extern const uint8_t* const p_video_font_000;

// 1KB glyph table for the HDMI renderer.
const uint8_t* roms_get_char_rom(bool video_graphics);
void roms_use_builtin_char_rom(void);
