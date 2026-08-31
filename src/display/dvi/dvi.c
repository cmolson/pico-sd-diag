// SPDX-License-Identifier: CC0-1.0
// https://github.com/dlehenbauer/econopet

#include "pch.h"
#include "dvi.h"

#include "crtc.h"
#include "roms/roms.h"
#include "system_state.h"
#include "tmds_encode.h"

// Tight core1 render loop instead of interrupt-driven scanline callbacks.
#define VIDEO_CORE1_LOOP

#define FRAME_WIDTH 720
#define FRAME_HEIGHT (480 / DVI_VERTICAL_REPEAT)
#define DVI_TIMING dvi_timing_720x480p_60hz

#define FONT_WIDTH 8
#define FONT_HEIGHT 8

#define WORDS_PER_LANE (FRAME_WIDTH / DVI_SYMBOLS_PER_WORD)

struct dvi_inst dvi0;
struct semaphore dvi_start_sem;

// C128 Palette (16 colors, RRRGGGBB)
static const uint8_t c128_palette[16] = {
    0x00, // 0: Black
    0x49, // 1: Medium Gray
    0x01, // 2: Blue
    0x03, // 3: Light Blue
    0x10, // 4: Green
    0x1C, // 5: Light Green
    0x0D, // 6: Dark Cyan
    0x1F, // 7: Light Cyan
    0x40, // 8: Red
    0xE0, // 9: Light Red
    0x81, // 10: Dark Magenta
    0xE3, // 11: Magenta
    0x6C, // 12: Dark Yellow
    0xDC, // 13: Yellow
    0xB6, // 14: Light Gray
    0xFF  // 15: White
};

// Color buffer: one byte per character, bg palette index high nibble, fg low.
#define MAX_CHARS_PER_LINE (FRAME_WIDTH / FONT_WIDTH)

// TMDS encoding wrappers over the assembly encoders in tmds_encode.S.

/**
 * Encode characters to TMDS for 80-column mode (8px wide characters).
 *
 * Calls the assembly tmds_encode_font_8px_palette_1lane for each RGB plane.
 *
 * @param charbuf       Character buffer (video RAM)
 * @param p_colorbuf    Color buffer (one byte per character)
 * @param tmdsbuf       Output TMDS buffer
 * @param n_chars       Number of characters to encode
 * @param font_base     Font bitmap base (8 bytes per character)
 * @param scanline_idx  Scanline index within character row (0-7, >7 = blank)
 * @param invert        0x00 normal, 0xFF to invert video
 */
static inline void __not_in_flash_func(tmds_encode_font_8px_palette)(
    const uint8_t *charbuf,
    const uint8_t *p_colorbuf,
    uint32_t *tmdsbuf,
    uint n_chars,
    const uint8_t *font_base,
    uint scanline_idx,
    uint32_t invert
) {
    const uint n_pix = n_chars * FONT_WIDTH;

    for (uint plane = 0; plane < N_TMDS_LANES; ++plane) {
        tmds_encode_font_8px_palette_1lane(
            charbuf,
            p_colorbuf,
            &tmdsbuf[plane * WORDS_PER_LANE],
            n_pix,
            font_base,
            scanline_idx,
            plane,
            invert
        );
    }
}

/**
 * Encode characters to TMDS for 40-column mode (16px wide characters, 2x stretch).
 *
 * Calls the assembly tmds_encode_font_16px_palette_1lane for each RGB plane.
 *
 * @param charbuf       Character buffer (video RAM)
 * @param p_colorbuf    Color buffer (one byte per character)
 * @param tmdsbuf       Output TMDS buffer
 * @param n_chars       Number of characters to encode
 * @param font_base     Font bitmap base (8 bytes per character)
 * @param scanline_idx  Scanline index within character row (0-7, >7 = blank)
 * @param invert        0x00 normal, 0xFF to invert video
 */
static inline void __not_in_flash_func(tmds_encode_font_16px_palette)(
    const uint8_t *charbuf,
    const uint8_t *p_colorbuf,
    uint32_t *tmdsbuf,
    uint n_chars,
    const uint8_t *font_base,
    uint scanline_idx,
    uint32_t invert
) {
    const uint n_pix = n_chars * FONT_WIDTH * 2;  // 2x stretch

    for (uint plane = 0; plane < N_TMDS_LANES; ++plane) {
        tmds_encode_font_16px_palette_1lane(
            charbuf,
            p_colorbuf,
            &tmdsbuf[plane * WORDS_PER_LANE],
            n_pix,
            font_base,
            scanline_idx,
            plane,
            invert
        );
    }
}

// Precalculated blank scanline, memcpy'd instead of re-encoded.
static uint32_t* blank_tmdsbuf;

// Copy blank margins (words per lane) into the target buffer.
static inline void copy_blank_margins(uint32_t *tmdsbuf, uint left_margin_words, uint right_margin_words) {
    const uint right_margin_start = WORDS_PER_LANE - right_margin_words;
    const size_t left_bytes = left_margin_words * sizeof(uint32_t);
    const size_t right_bytes = right_margin_words * sizeof(uint32_t);
    uint32_t* dst_lane = tmdsbuf;
    uint32_t* src_lane = blank_tmdsbuf;

    uint remaining = N_TMDS_LANES;
    while (remaining--) {
        memcpy(dst_lane, src_lane, left_bytes);
        memcpy(dst_lane + right_margin_start, src_lane + right_margin_start, right_bytes);
        dst_lane += WORDS_PER_LANE;
        src_lane += WORDS_PER_LANE;
    }
}

volatile uint32_t dvi_scanline_count;  // DIAG: proves core1 is pumping scanlines

static inline void __not_in_flash_func(prepare_scanline)(uint16_t y) {
    dvi_scanline_count++;
    static dvi_display_geometry_t geo = {
        .chars_per_row = 40,
        .rows = 25,
        .scanlines_per_row = 8,
        .vram_start = 0x000,
        .vram_mask = 0x3ff,
        .invert_mask = 0x00,
        .visible_scanlines = 200,
        .top_margin = 20,
        .double_width = true,
        .left_margin_words = 0,
        .content_words = 0,
        .right_margin_words = 0,
    };

    // TODO: Copy character into SRAM and precalculate flip/stretch? (PERF)
    static const uint8_t* p_char_rom = rom_chars_e800;

    uint8_t* const video_char_buffer = system_state.video_char_buffer;
    uint8_t* const colorbuf = video_char_buffer + 0x800;

    // y==0 is the first visible line; unsigned wrap puts the top blanks above.
    y -= geo.top_margin;

    if (y >= geo.visible_scanlines) {
        // Blank line: recompute CRTC-dependent state.
        crtc_calculate_geometry(
            system_state.pet_crtc_registers,
            system_state.pet_display_columns,
            FRAME_WIDTH,
            FRAME_HEIGHT,
            FONT_WIDTH,
            DVI_SYMBOLS_PER_WORD,
            &geo
        );

        p_char_rom = roms_get_char_rom(system_state.video_graphics);

        uint32_t *tmdsbuf;
        queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);
        memcpy(tmdsbuf, blank_tmdsbuf, N_TMDS_LANES * WORDS_PER_LANE * sizeof(uint32_t));
        queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
    } else {
        const uint row_offset = geo.vram_start + y / geo.scanlines_per_row * geo.chars_per_row;
        const uint row_start = row_offset & geo.vram_mask;

        const uint ra = y % geo.scanlines_per_row;

        uint32_t *tmdsbuf;
        queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);

        copy_blank_margins(tmdsbuf, geo.left_margin_words, geo.right_margin_words);

        // The row may wrap around the (vram_mask + 1)-sized buffer.
        const uint buffer_size = geo.vram_mask + 1;
        const uint first_chars = MIN(buffer_size - row_start, geo.chars_per_row);

        if (geo.double_width) {
            tmds_encode_font_16px_palette(
                &video_char_buffer[row_start],
                &colorbuf[row_start],
                &tmdsbuf[geo.left_margin_words],
                first_chars,
                p_char_rom,
                ra,
                geo.invert_mask
            );
        } else {
            tmds_encode_font_8px_palette(
                &video_char_buffer[row_start],
                &colorbuf[row_start],
                &tmdsbuf[geo.left_margin_words],
                first_chars,
                p_char_rom,
                ra,
                geo.invert_mask
            );
        }

        if (first_chars < geo.chars_per_row) {
            const uint second_chars = geo.chars_per_row - first_chars;
            uint first_words = first_chars * FONT_WIDTH / DVI_SYMBOLS_PER_WORD;

            if (geo.double_width) {
                first_words *= 2;   // 40-column mode: each character is 16 pixels wide
                tmds_encode_font_16px_palette(
                    &video_char_buffer[0],
                    &colorbuf[0],
                    &tmdsbuf[geo.left_margin_words + first_words],
                    second_chars,
                    p_char_rom,
                    ra,
                    geo.invert_mask
                );
            } else {
                tmds_encode_font_8px_palette(
                    &video_char_buffer[0],
                    &colorbuf[0],
                    &tmdsbuf[geo.left_margin_words + first_words],
                    second_chars,
                    p_char_rom,
                    ra,
                    geo.invert_mask
                );
            }
        }

        queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
    }
}

#ifdef VIDEO_CORE1_LOOP

// Tight loop mode.
static void __not_in_flash_func(core1_main)() {
    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    sem_acquire_blocking(&dvi_start_sem);
    dvi_start(&dvi0);

    while (true) {
        for (uint y = 0; y < FRAME_HEIGHT; ++y) {
            prepare_scanline(y);
        }
    }
    __builtin_unreachable();
}

#else

// Interrupt mode.
static void __not_in_flash_func(core1_scanline_callback)() {
    static uint y = 0;
    prepare_scanline(y);
    y = (y + 1) % FRAME_HEIGHT;
}

static void __not_in_flash_func(core1_main)() {
    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    sem_acquire_blocking(&dvi_start_sem);
    dvi_start(&dvi0);

    while (1) {
        __wfi();
    }
    __builtin_unreachable();
}

#endif // VIDEO_CORE1_LOOP

void video_init() {
    const uint32_t f_clk_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
    const int32_t delta = f_clk_sys - DVI_TIMING.bit_clk_khz;
    if (!(-1 <= delta && delta <= 1)) {
        panic("FAIL: Incorrect clk_sys frequency. Expected %d +/-1 kHz, but got %d kHz.", DVI_TIMING.bit_clk_khz, f_clk_sys);
    }

    dvi0.timing = &DVI_TIMING;
    dvi0.ser_cfg = micromod_cfg;
#ifndef VIDEO_CORE1_LOOP
    dvi0.scanline_callback = core1_scanline_callback;
#endif
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

    // Keep one free-queue buffer permanently as the blank scanline.
    queue_remove_blocking(&dvi0.q_tmds_free, &blank_tmdsbuf);

    uint8_t* const video_char_buffer = system_state.video_char_buffer;
    uint8_t* const colorbuf = video_char_buffer + 0x800;
    
    set_palette(c128_palette, c128_palette);

    // Precalculate the blank scanline.
    tmds_encode_font_8px_palette(
        video_char_buffer,          // charbuf (content ignored for blank lines)
        colorbuf,                   // colorbuf
        blank_tmdsbuf,
        FRAME_WIDTH / FONT_WIDTH,   // n_chars
        p_video_font_000,           // font_base
        FONT_HEIGHT,                // ra >= font height = blank line (shows background only)
        0x00                        // no invert
    );

#ifndef VIDEO_CORE1_LOOP
    dvi0.scanline_callback();
#endif

    sem_init(&dvi_start_sem, /* initial_permits: */ 0, /* max_permits: */ 1);
    hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
    multicore_launch_core1(core1_main);
    sem_release(&dvi_start_sem);
}
