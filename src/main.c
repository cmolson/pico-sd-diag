// SPDX-License-Identifier: CC0-1.0

#include "pch.h"

#include "display/dvi/dvi.h"

extern void __attribute__((noreturn)) sd_diag_run(void);

int main() {
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);

    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
    set_sys_clock_khz(270000, true);

    stdio_init_all();
    video_init();
    sd_diag_run();
}
