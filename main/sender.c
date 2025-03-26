#include "sender.h"

#include <rtc_wdt.h>
#include <driver/gpio.h>
#include <rom/ets_sys.h>

#define LED_GPIO         17

void send_manchester_bit(const int bit, const int half_period_us) {
    if (bit == 0) {
        gpio_set_level(LED_GPIO, 1);
        ets_delay_us(half_period_us);
        gpio_set_level(LED_GPIO, 0);
        ets_delay_us(half_period_us);
    } else if (bit == 1) {
        gpio_set_level(LED_GPIO, 0);
        ets_delay_us(half_period_us);
        gpio_set_level(LED_GPIO, 1);
        ets_delay_us(half_period_us);
    } else if (bit == -1) {
        gpio_set_level(LED_GPIO, 0);
        ets_delay_us(half_period_us * 2);
    } else if (bit == 2) {
        gpio_set_level(LED_GPIO, 1);
        ets_delay_us(half_period_us * 2);
    }
    rtc_wdt_feed();
}

void process_binary_data(const uint8_t* data, const int len, const double baseFrequency) {
    int half_period_us = (int)(1000000.0 / baseFrequency / 2);
    if (half_period_us < 1) half_period_us = 1;

    for (int i = 0; i < 4; i++) {
        send_manchester_bit(0, half_period_us);
    }
    for (int i = 0; i < 3; i++) {
        send_manchester_bit(1, half_period_us);
    }
    send_manchester_bit(-1, half_period_us);

    const int period_us = (int)(1000000.0 / baseFrequency);
    ets_delay_us(period_us);
    for (int i = 0; i < len; i++) {
        const uint8_t data_byte = data[i];
        for (int bit = 7; bit >= 0; bit--) {
            const int current = (data_byte >> bit) & 1;
            send_manchester_bit(current, half_period_us);
        }
    }

    gpio_set_level(LED_GPIO, 0);
}
