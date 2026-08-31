// SPDX-License-Identifier: CC0-1.0
//
// Pin configuration (defaults: EconoPET 40/8096 rev A).

#pragma once

// SD card on SPI1.
#define SD_SPI_INSTANCE spi1
#define SD_CLK_GP 14        // SPI1 SCK  -> SD CLK
#define SD_CMD_GP 11        // SPI1 TX   -> SD CMD (MOSI)
#define SD_DAT_GP 12        // SPI1 RX   -> SD DAT0 (MISO)
#define SD_CSN_GP 9         // chip select, driven as GPIO
#define SD_DETECT 8         // card detect switch (either polarity; optional)

// Data-phase SPI clock. >24 MHz needs a peri_clk overclock or PIO.
#define SD_SPI_MHZ 24

// DVI: micromod_cfg -- clock GP16/17, TMDS GP18/19, 20/21, 22/23
// (retarget via dvi0.ser_cfg in dvi.c).

// Status LED: PICO_DEFAULT_LED_PIN (GP25 on a stock Pico).
