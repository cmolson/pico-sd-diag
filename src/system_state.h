// SPDX-License-Identifier: CC0-1.0
// Minimal system state shared with the DVI renderer (pruned from the
// EconoPET firmware; only the fields the display and diag code touch).

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Upper bound on video RAM addressed by the CRTC (8KB, as in the 8296).
#define PET_MAX_VIDEO_RAM_BYTES 0x2000

// Number of CRTC (6545) registers
#define CRTC_REG_COUNT 14

// CRTC (6545) register indices
// http://archive.6502.org/datasheets/rockwell_r6545-1_crtc.pdf
#define CRTC_R0_H_TOTAL             0   // [7:0] Total characters per horizontal line, minus one.
#define CRTC_R1_H_DISPLAYED         1   // [7:0] Displayed characters per horizontal line.
#define CRTC_R2_H_SYNC_POS          2   // [7:0] HSYNC position.
#define CRTC_R3_SYNC_WIDTH          3   // [3:0] HSYNC width, [7:4] VSYNC width
#define CRTC_R4_V_TOTAL             4   // [6:0] Total character rows per frame, minus one.
#define CRTC_R5_V_ADJUST            5   // [4:0] Additional scan lines to complete a frame.
#define CRTC_R6_V_DISPLAYED         6   // [6:0] Displayed character rows per frame.
#define CRTC_R7_V_SYNC_POS          7   // [6:0] Character row of VSYNC pulse.
#define CRTC_R8_MODE_CONTROL        8   // [7:0] Operating mode (not implemented)
#define CRTC_R9_MAX_SCAN_LINE       9   // [4:0] Scan lines per character row, minus one.
#define CRTC_R10_CURSOR_START_LINE  10  // (not implemented)
#define CRTC_R11_CURSOR_END_LINE    11  // (not implemented)
#define CRTC_R12_START_ADDR_HI      12  // [5:0] High 6 bits of display start address.
#define CRTC_R13_START_ADDR_LO      13  // [7:0] Low 8 bits of display start address.

typedef enum pet_display_columns_e {
    pet_display_columns_40 = 40,
    pet_display_columns_80 = 80,
} pet_display_columns_t;

typedef enum video_source_e {
    video_source_pet,       // (unused here; kept for diag log compatibility)
    video_source_firmware,  // HDMI shows firmware-controlled buffer
} video_source_t;

typedef struct system_state_s {
    // Number of displayed columns (40 or 80).
    pet_display_columns_t pet_display_columns;

    // Video RAM mask (0-3): (mask + 1) KB of video RAM.
    uint8_t video_ram_mask;

    // Precomputed size of video RAM in bytes.
    size_t video_ram_bytes;

    video_source_t video_source;

    // Video character buffer rendered by the DVI core.
    uint8_t video_char_buffer[PET_MAX_VIDEO_RAM_BYTES];

    // Charset select (false = text/business, true = graphics).
    bool video_graphics;

    // CRTC (6545) registers controlling video timing.
    uint8_t pet_crtc_registers[CRTC_REG_COUNT];
} system_state_t;

extern system_state_t system_state;

// Setter to keep derived fields in sync.
void system_state_set_video_ram_mask(system_state_t* state, uint8_t video_ram_mask);
