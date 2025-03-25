#include "sender.h"

#include <driver/gpio.h>
#include <rom/ets_sys.h>

#define LED_GPIO         17

void send_manchester_bit(const int bit, const double baseFrequency) {
    int half_period_us = (int)(1000000.0 / baseFrequency / 2);
    if (half_period_us < 1) half_period_us = 1;
    if (bit == 0) {
        gpio_set_level(LED_GPIO, 1);
        ets_delay_us(half_period_us);
        gpio_set_level(LED_GPIO, 0);
        ets_delay_us(half_period_us);
    } else {
        gpio_set_level(LED_GPIO, 0);
        ets_delay_us(half_period_us);
        gpio_set_level(LED_GPIO, 1);
        ets_delay_us(half_period_us);
    }
}

void process_binary_data(const uint8_t* data, const int len, const double baseFrequency) {
    for (int i = 0; i < len; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            const int current = (data[i] >> bit) & 1;
            send_manchester_bit(current, baseFrequency);
        }
    }
    // По окончании передачи устанавливаем низкий уровень на выходе.
    gpio_set_level(LED_GPIO, 0);
}
