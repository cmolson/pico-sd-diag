// SPDX-License-Identifier: CC0-1.0
// Common includes shared by all sources (adapted from the EconoPET firmware).

#pragma once

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// Standard includes
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Pico SDK
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/sem.h"
#include "pico/stdlib.h"
#include "pico/types.h"

// PicoDVI
#include "common_dvi_pin_configs.h"
#include "dvi_serialiser.h"
#include "dvi.h"
#include "tmds_encode.h"

// TinyUSB
#include "tusb.h"
