# pico-sd-diag

SD card compatibility tester for RP2040 boards. Flash it, plug in a monitor,
and feed it SD cards: for each card it runs a battery of init, raw-read, and
filesystem tests and prints a one-line verdict. It grew out of debugging SD
card boot failures on the [EconoPET](https://github.com/dlehenbauer/econopet)
and extracts that project's diagnostic build into a standalone firmware.

The firmware boots entirely from flash -- no SD card is needed to start. Results
appear on:

- an 80x25 scrolling text console over DVI/HDMI (bit-banged by the RP2040 via
  [PicoDVI](https://github.com/Wren6991/PicoDVI), using the Commodore PET
  character ROM), and
- a USB CDC serial console that mirrors everything, and
- the default UART (GP0/GP1, 115200).

If a card mounts, the full session log is also written to `SDDIAG.TXT` on the
card, along with display-state forensics.

## What the tests mean

Each pass prints:

1. `init @10 mhz` -- SPI-mode card init at a 10 MHz init clock. On failure it
   retries at the standard 400 kHz; a card that only inits slow is flagged in
   the summary.
2. `single block (cmd17)` -- one raw 512-byte read. On failure the raw R1
   response trace is printed (`resp: ...`).
3. `multi block, old cmd12` -- an 8-block CMD18 read terminated with the old
   driver's stop-transmission sequence (deselect first, strict R1 check).
4. `multi block, new cmd12` -- the same read with the fixed CMD12 handling.
   Together, 3 and 4 are an A/B test of the stop-transmission fix.
5. `bulk sweep` -- 2 MB of raw sequential reads at the full data clock
   (24 MHz). If it fails, it retries with CMD17-only single-block reads, then
   again at successively halved clocks, to find what the card can sustain.
6. `mount` -- FAT mount attempt. On failure the boot sector is inspected and
   classified: unformatted, exFAT (unsupported), or FAT-but-damaged.

The summary then reports each quirk and a verdict line:

- `no quirks - fine even with the old driver`
- `ALL QUIRKS FIXED by the new driver`
- `NOT FULLY FIXED - needs cmd17-only reads` / `a slower data clock`
- `DEAD CARD (no init at 10 mhz or 400 khz)`

Swap cards freely: a card-detect edge (either polarity) or a 10 second timeout
triggers the next pass.

## Building

```
git clone --recursive https://github.com/cmolson/pico-sd-diag
cd pico-sd-diag
export PICO_SDK_PATH=/path/to/pico-sdk   # 2.1.1 or later
cmake -B build && cmake --build build
```

Produces `build/pico_sd_diag.uf2`. Every push also builds it on GitHub
Actions, and tagged releases attach the uf2 -- no toolchain needed.

## Flashing

Put the RP2040 into its USB bootloader, then copy `build/pico_sd_diag.uf2`
to the `RPI-RP2` drive that appears.

- EconoPET: hold the FLASH button while connecting USB (or while pressing reset).
- Stock Pico boards: hold BOOTSEL while plugging in.

## Serial console

The board enumerates as a USB CDC serial device ("SD Diagnostic"). Connect at
any baud rate with a terminal that **asserts DTR** (picocom, minicom, PuTTY;
`picocom /dev/ttyACM0` works out of the box). On connect, the full history of
the session so far is replayed, so you can attach after the fact and still see
everything.

## Pins (defaults: EconoPET 40/8096 rev A)

All pin choices live in `src/board.h` (SD) and the PicoDVI `micromod_cfg`
serializer config (DVI); adjust for your wiring.

| Signal      | GPIO  | Notes                              |
|-------------|-------|------------------------------------|
| SD CLK      | 14    | SPI1 SCK                           |
| SD CMD      | 11    | SPI1 TX (MOSI)                     |
| SD DAT0     | 12    | SPI1 RX (MISO)                     |
| SD CS       | 9     | driven as GPIO                     |
| SD detect   | 8     | optional, either polarity          |
| DVI clock   | 16/17 | differential pair                  |
| DVI TMDS 0-2| 18/19, 20/21, 22/23 | differential pairs   |
| LED         | 25    | `PICO_DEFAULT_LED_PIN`, heartbeat  |
| UART TX/RX  | 0/1   | 115200, mirrors the console        |

The system clock is overclocked to 270 MHz (1.20 V core) to generate the
720x480 DVI signal.

## Dependencies

`external/` holds git submodules (clone with `--recursive`, or run
`git submodule update --init` after a plain clone):

- `external/pico-vfs` -- [pico-vfs](https://github.com/oyama/pico-vfs) fork
  (`cmolson/pico-vfs`, `sd-diag` branch) with the CMD12 fixes and the
  diagnostic hooks this firmware drives (`sd_diag_init_hz`,
  `sd_diag_resp_trace*`, `sd_diag_force_cmd17`, `sd_diag_legacy_cmd12`).
- `external/PicoDVI` -- [PicoDVI](https://github.com/Wren6991/PicoDVI)
  (`DLehenbauer/PicoDVI` fork, `oob_fix` branch).

## License

CC0-1.0, like the EconoPET firmware it derives from. Submodules keep
their own licenses (see `external/*/LICENSE*`).
