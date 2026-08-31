// SPDX-License-Identifier: CC0-1.0
//
// SD card diagnostic: paints card init/mount results on HDMI using the
// built-in font (boots entirely from flash; no SD card required).
#include "pch.h"

#include "blockdevice/sd.h"
#include "filesystem/fat.h"
#include "filesystem/vfs.h"
#include "display/window.h"
#include "roms/roms.h"
#include <ctype.h>
#include "system_state.h"
#include "board.h"
#include "tusb.h"

uint32_t sd_diag_init_hz = 0;   // runtime hook into the patched pico-vfs
extern volatile uint32_t dvi_scanline_count;
extern uint8_t sd_diag_resp_trace[16];
extern int sd_diag_resp_trace_n;
extern int sd_diag_trace_cmd;
extern int sd_diag_force_cmd17;
extern int sd_diag_legacy_cmd12;

static const window_t* W;
static uint8_t* cur;

// Full history; dumped to /SDDIAG.TXT whenever a card mounts.
static char hist[8192];
static size_t hist_len;

static void hist_append(const char* line) {
    size_t n = strlen(line);
    if (hist_len + n + 1 >= sizeof(hist)) return;
    memcpy(hist + hist_len, line, n);
    hist_len += n;
    hist[hist_len++] = '\n';
}

static void say(const char* fmt, ...);

// Sleep while keeping the CDC console alive.
static void diag_sleep_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        tud_task();
        sleep_ms(1);
    }
}

static void cdc_write_all(const char* data, size_t n) {
    if (!tud_cdc_connected()) return;
    while (n > 0) {
        uint32_t w = tud_cdc_write(data, n);
        tud_cdc_write_flush();
        tud_task();
        data += w;
        n -= w;
        if (w == 0) sleep_ms(1);
    }
}

// Replay history when a terminal attaches.
static void cdc_service(void) {
    static bool greeted = false;
    tud_task();
    if (tud_cdc_connected() && !greeted) {
        greeted = true;
        cdc_write_all("\r\n=== SD DIAG console: replaying history ===\r\n", 46);
        cdc_write_all(hist, hist_len);
        cdc_write_all("=== live from here ===\r\n", 25);
    }
}

static void dump_log_to_card(void) {
    FILE* f = fopen("/SDDIAG.TXT", "w");
    if (!f) { say("log write FAILED (%d)", errno); return; }

    fwrite(hist, 1, hist_len, f);

    // Display forensics.
    uint32_t a = dvi_scanline_count;
    sleep_ms(100);
    uint32_t b = dvi_scanline_count;
    fprintf(f, "\n-- display state --\n");
    fprintf(f, "clk_sys=%lu kHz\n",
            (unsigned long) frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS));
    fprintf(f, "scanlines: %lu (+%lu in 100ms; ~15748 expected)\n",
            (unsigned long) b, (unsigned long) (b - a));
    fprintf(f, "video_source=%d columns=%d\n",
            system_state.video_source, system_state.pet_display_columns);
    const uint8_t* vb = system_state.video_char_buffer;
    for (int r = 0; r < 4; r++) {
        fprintf(f, "row%d:", r);
        for (int c = 0; c < 40; c++) fprintf(f, " %02x", vb[r * 40 + c]);
        fprintf(f, "\n");
    }
    fprintf(f, "attr[0..7]:");
    for (int c = 0; c < 8; c++) fprintf(f, " %02x", vb[0x800 + c]);
    fprintf(f, "\ncrtc:");
    for (int c = 0; c < CRTC_REG_COUNT; c++) fprintf(f, " %02x", system_state.pet_crtc_registers[c]);
    fprintf(f, "\n");
    fclose(f);
    say("log written to /SDDIAG.TXT");
}

void sd_diag_blink(unsigned int n) {
    for (unsigned int i = 0; i < n; i++) {
        gpio_put(PICO_DEFAULT_LED_PIN, 0); sleep_ms(150);
        gpio_put(PICO_DEFAULT_LED_PIN, 1); sleep_ms(150);
    }
    sleep_ms(400);
}

static void say(const char* fmt, ...) {
    char line[81];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    // Shifted screen codes are graphics glyphs; lowercase renders as uppercase.
    for (char* p = line; *p; p++) *p = (char) tolower((unsigned char) *p);
    // Scroll when full.
    if (cur >= W->start + W->width * (W->height - 1)) {
        memmove(W->start, W->start + W->width, W->width * (W->height - 1));
        memset(W->start + W->width * (W->height - 1), CH_SPACE, W->width);
        cur = W->start + W->width * (W->height - 1);
    }
    cur = window_println(W, cur, "%s", line);
    printf("%s\n", line);   // mirror to UART
    hist_append(line);
    cdc_write_all(line, strlen(line));
    cdc_write_all("\r\n", 2);
}

// One raw read: 1 block = CMD17, more = CMD18. Traces failures.
static bool raw_block_test(blockdevice_t* sd, unsigned int blocks) {
    static uint8_t buf[4096];
    sd_diag_resp_trace_n = 0;
    int st = sd->read(sd, buf, 0, blocks * 512);
    if (st != 0 && sd_diag_resp_trace_n > 0) {
        char hex[41];
        int n = sd_diag_resp_trace_n > 13 ? 13 : sd_diag_resp_trace_n;
        for (int i = 0; i < n; i++)
            snprintf(hex + i * 3, 4, "%02x ", sd_diag_resp_trace[i]);
        say("  resp: %s", hex);
    }
    return st == 0;
}

// Raw sweep; needs no filesystem.
static bool raw_bulk_test(blockdevice_t* sd, uint32_t bytes) {
    static uint8_t buf[4096];
    uint32_t sum = 0;
    absolute_time_t t0 = get_absolute_time();
    for (uint32_t off = 0; off < bytes; off += sizeof(buf)) {
        if (sd->read(sd, buf, off, sizeof(buf)) != 0) {
            say("   FAILED at byte %lu", (unsigned long) off);
            return false;
        }
        for (size_t i = 0; i < sizeof(buf); i += 64) sum += buf[i];
    }
    int64_t us = absolute_time_diff_us(t0, get_absolute_time());
    say("   %lu kb ok, %lu kb/s (sum %08lx)", (unsigned long) (bytes / 1024),
        (unsigned long) (us > 0 ? (bytes * 977ull) / us : 0), (unsigned long) sum);
    return true;
}

// Classify why a mount failed from the raw sectors.
static void classify_format(blockdevice_t* sd) {
    static uint8_t sec[512];
    if (sd->read(sd, sec, 0, 512) != 0) { say("   (sector 0 unreadable)"); return; }
    for (int hop = 0; hop < 2; hop++) {
        if (sec[510] != 0x55 || sec[511] != 0xAA) {
            say("   format: NONE (no boot signature - card unformatted?)");
            return;
        }
        if (memcmp(sec + 3, "EXFAT   ", 8) == 0) {
            say("   format: EXFAT - unsupported, reformat as fat32");
            return;
        }
        if (memcmp(sec + 54, "FAT1", 4) == 0 || memcmp(sec + 82, "FAT32", 5) == 0) {
            say("   format: fat present but mount failed - damaged?");
            return;
        }
        // MBR: follow partition 1 once.
        if (hop == 0 && sec[450] != 0) {
            uint32_t lba = (uint32_t) sec[454] | ((uint32_t) sec[455] << 8)
                         | ((uint32_t) sec[456] << 16) | ((uint32_t) sec[457] << 24);
            if (lba && sd->read(sd, sec, (uint64_t) lba * 512, 512) == 0) continue;
        }
        say("   format: unrecognized (not fat12/16/32)");
        return;
    }
}

void __attribute__((noreturn)) sd_diag_run(void) {
    // Never returns, so stack-local is fine.
    system_state.pet_display_columns = pet_display_columns_80;
    system_state_set_video_ram_mask(&system_state, 1);   // 2KB VRAM for 80 col
    const window_t win = window_create(system_state.video_char_buffer, 80, 25);
    W = &win;

    roms_use_builtin_char_rom();   // else the DVI font is all zeros

    // Attrs default to 0: black on black.
    uint8_t* const colorbuf = system_state.video_char_buffer + 0x800;
    memset(colorbuf, 0x0F, PET_MAX_VIDEO_RAM_BYTES - 0x800);

    gpio_init(SD_CSN_GP);
    gpio_set_dir(SD_CSN_GP, GPIO_OUT);
    gpio_put(SD_CSN_GP, 1);

    gpio_init(SD_DETECT);
    gpio_set_dir(SD_DETECT, GPIO_IN);
    gpio_pull_up(SD_DETECT);

    fs_unmount("/");              // release the boot mount; the probe owns the bus

    tud_init(0);                  // USB serial console (host stack never started)

    for (unsigned int pass = 1;; pass++) {
        window_fill(&win, ' ');
        cur = win.start;
        sd_diag_force_cmd17 = 0;
        say("*** SD CARD DIAGNOSTIC #%u ***", pass);
        say("standalone build");
        say("");

        blockdevice_t* sd;
        filesystem_t* fat = NULL;
        int rc;
        bool init_ok, slow_init = false, cmd17_ok = false;
        bool old12_ok = false, new12_ok = false;
        bool mount_ok = false, bulk24_ok = false, bulk_ok = false, need17 = false;
        unsigned int max_mhz = 0;

        sd_diag_init_hz = 10 * 1000 * 1000;
        sd = blockdevice_sd_create(
            SD_SPI_INSTANCE, SD_CMD_GP, SD_DAT_GP, SD_CLK_GP, SD_CSN_GP,
            SD_SPI_MHZ * MHZ, /* enable_crc: */ false);
        rc = sd->init(sd);
        say("1. init @10 mhz: %s (rc=%d)", rc == 0 ? "ok" : "FAILED", rc);
        if (rc != 0) {
            blockdevice_sd_free(sd);
            diag_sleep_ms(100);
            sd_diag_init_hz = 400 * 1000;
            sd = blockdevice_sd_create(
                SD_SPI_INSTANCE, SD_CMD_GP, SD_DAT_GP, SD_CLK_GP, SD_CSN_GP,
                SD_SPI_MHZ * MHZ, /* enable_crc: */ false);
            rc = sd->init(sd);
            say("   init @400 khz: %s (rc=%d)", rc == 0 ? "ok" : "FAILED", rc);
            slow_init = (rc == 0);
        }
        init_ok = (rc == 0);

        if (init_ok) {
            say("   size %lu mb", (unsigned long) (sd->size(sd) / (1024 * 1024)));

            sd_diag_trace_cmd = 18;
            cmd17_ok = raw_block_test(sd, 1);
            say("2. single block (cmd17): %s", cmd17_ok ? "ok" : "FAILED");

            sd_diag_legacy_cmd12 = 1;
            old12_ok = raw_block_test(sd, 8);
            say("3. multi block, old cmd12: %s", old12_ok ? "ok" : "FAILED");
            sd_diag_legacy_cmd12 = 0;
            raw_block_test(sd, 1);   // resync after a possibly bad stop

            new12_ok = raw_block_test(sd, 8);
            say("4. multi block, new cmd12: %s", new12_ok ? "ok" : "FAILED");
            sd_diag_trace_cmd = -1;

            say("5. bulk sweep @%u mhz (2 mb):", SD_SPI_MHZ);
            bulk_ok = bulk24_ok = raw_bulk_test(sd, 2u << 20);
            if (bulk_ok) max_mhz = SD_SPI_MHZ;

            if (!bulk_ok) {
                sd_diag_force_cmd17 = 1;
                say("   cmd17-only fallback:");
                bulk_ok = raw_bulk_test(sd, 2u << 20);
                if (bulk_ok) { need17 = true; max_mhz = SD_SPI_MHZ; }
                else sd_diag_force_cmd17 = 0;
            }
            unsigned int mhz = SD_SPI_MHZ;
            while (!bulk_ok && mhz > 3) {
                mhz /= 2;
                blockdevice_sd_free(sd);
                sd = blockdevice_sd_create(
                    SD_SPI_INSTANCE, SD_CMD_GP, SD_DAT_GP, SD_CLK_GP, SD_CSN_GP,
                    mhz * MHZ, /* enable_crc: */ false);
                if (sd->init(sd) != 0) { say("   %u mhz re-init FAILED", mhz); break; }
                say("   retry @%u mhz:", mhz);
                bulk_ok = raw_bulk_test(sd, 2u << 20);
                if (bulk_ok) max_mhz = mhz;
            }

            fat = filesystem_fat_create();
            mount_ok = (fs_mount("/", fat, sd) != -1);
            say("6. mount: %s%s", mount_ok ? "ok" : "FAILED ",
                mount_ok ? "" : strerror(errno));
            if (!mount_ok) classify_format(sd);
        }

        say("");
        say("--- summary ---");
        if (!init_ok) {
            say("verdict: DEAD CARD (no init at 10 mhz or 400 khz)");
        } else {
            say("10 mhz init: %s", slow_init ? "NO - needs 400 khz" : "yes");
            say("old cmd12:   %s", old12_ok ? "tolerated" : "REJECTED");
            say("new cmd12:   %s", new12_ok ? "works" : "STILL FAILS");
            if (!mount_ok)           say("filesystem:  not mountable (content, not card health)");
            if (bulk24_ok)           say("data @%u mhz: full speed", SD_SPI_MHZ);
            else if (need17)         say("data: needs cmd17-only reads");
            else if (max_mhz)        say("data: max %u mhz", max_mhz);
            else                     say("data: UNREADABLE at any speed");

            bool old_driver_ok = !slow_init && old12_ok && bulk24_ok;
            bool fix_covers    = new12_ok && bulk24_ok && !need17;
            if (old_driver_ok)
                say("verdict: no quirks - fine even with the old driver");
            else if (fix_covers)
                say("verdict: ALL QUIRKS FIXED by the new driver");
            else if (bulk_ok)
                say("verdict: NOT FULLY FIXED - needs %s",
                    need17 ? "cmd17-only reads" : "a slower data clock");
            else
                say("verdict: NOT FULLY FIXED - see failures above");
        }
        if (mount_ok) {
            dump_log_to_card();
            fs_unmount("/");
        }
        if (fat) filesystem_fat_free(fat);
        blockdevice_sd_free(sd);

        say("");
        say("swap card; retests on change or 10s");

        // Re-probe on a detect edge or every 10 s.
        const bool level = gpio_get(SD_DETECT);
        for (unsigned int i = 0; i < 100; i++) {
            if (gpio_get(SD_DETECT) != level) {
                diag_sleep_ms(500);   // card seating / contact bounce
                break;
            }
            gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);  // heartbeat
            cdc_service();
            diag_sleep_ms(100);
        }
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
    }
}
