// Тестовая прошивка для проверки программатора RP2040 по SWD.
// Мигает встроенным LED Raspberry Pi Pico (GPIO25) с периодом BLINK_MS.

#include "pico/stdlib.h"

#ifndef BLINK_MS
#define BLINK_MS 500
#endif

int main(void) {
    gpio_init(25);
    gpio_set_dir(25, GPIO_OUT);

    while (1) {
        gpio_put(25, 1);
        sleep_ms(BLINK_MS);
        gpio_put(25, 0);
        sleep_ms(BLINK_MS);
    }
}
