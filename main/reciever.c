#include "reciever.h"

#include <esp_timer.h>
#include <rtc.h>
#include <rtc_wdt.h>
#include <stdio.h>
#include <string.h>
#include <driver/adc.h>
#include <driver/uart.h>
#include <rom/ets_sys.h>

#include "synchronizer.h"

#define BUFFER_SIZE 1024

int receive_bit(const int threshold) {
    return adc1_get_raw(ADC1_CHANNEL_4) < threshold;
}

static uint8_t received_byte_buffer[BUFFER_SIZE];
static uint8_t test_buffer[BUFFER_SIZE];

void process_manchester_receive(
    const int threshold, const double baseFrequency,
    const uart_port_t uart_port
) {
    for (int i = 0; i < BUFFER_SIZE; ++i) {
        received_byte_buffer[i] = 0;
    }
    if (!await_end_sync(threshold)) {
        return;
    }
    // const char* console_buffer = "HANDLED!!!!!!!!\r\n\0";
    // uart_write_bytes(UART_NUM_0, console_buffer, strlen(console_buffer));

    int buffer_index = 0;
    bool received_byte[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int bit_count = 0;

    // Расчёт целевого интервала для одного полупериода в микросекундах
    int max_delay_period_us = (int)(1000000.0 / baseFrequency * 10);
    int half_period_target_us = (int)(1000000.0 / baseFrequency / 2);
    if (half_period_target_us < 1) half_period_target_us = 1;

    uint64_t last_signal_time = esp_timer_get_time(); // Время последнего изменения сигнала

    for (int i = 0; i < (half_period_target_us / 2) / 100; ++i) {
        rtc_wdt_feed();
        ets_delay_us(100);
    }

    int test_off = 0;

    while (1) {
        rtc_wdt_feed();
        const uint64_t now = esp_timer_get_time();

        // Выход по таймауту, если сигнал не изменялся
        if (now - last_signal_time > max_delay_period_us) {
            break;
        }

        const int first_signal = (adc1_get_raw(ADC1_CHANNEL_4) >= threshold) ? 1 : 0;
        for (int i = 0; i < (half_period_target_us / 100); ++i) {
            rtc_wdt_feed();
            ets_delay_us(100);
        }

        const int second_signal = (adc1_get_raw(ADC1_CHANNEL_4) >= threshold) ? 1 : 0;
        for (int i = 0; i < (half_period_target_us / 100); ++i) {
            rtc_wdt_feed();
            ets_delay_us(100);
        }

        test_buffer[test_off++] = first_signal ? '1' : '0';
        test_buffer[test_off++] = second_signal ? '1' : '0';
        test_buffer[test_off++] = ' ';

        int bit = -1;
        if (first_signal == 0 && second_signal == 1) {
            bit = 1;
        } else if (first_signal == 1 && second_signal == 0) {
            bit = 0;
        } else if (first_signal == 1 && second_signal == 1) {
            bit = 2;
        }

        test_buffer[test_off++] = '(';
        if (bit == -1) {
            test_buffer[test_off++] = '-';
            test_buffer[test_off++] = '1';
        } else if (bit == 1) {
            test_buffer[test_off++] = '1';
        } else if (bit == 0) {
            test_buffer[test_off++] = '0';
        } else if (bit == 2) {
            test_buffer[test_off++] = '2';
        }
        test_buffer[test_off++] = ')';

        // char aaa[96];
        // aaa[snprintf(aaa, sizeof(aaa), "Bit %d%d \r\n", first_signal, second_signal)] = '\0';
        // uart_write_bytes(UART_NUM_0, aaa, strlen(aaa));

        if (bit == -1) {
            bit = 0;
        } else if (bit == 2) {
            bit = 1;
        } else {
            last_signal_time = now;
        }

        bit_count++;

        received_byte[bit_count] = bit;
        unsigned char byte_value = 0;
        if (bit_count >= 8) {
            for (int i = 0; i < 8; i++) {
                byte_value |= (received_byte[i] << (7 - i));
                received_byte[i] = 0;
            }
            bit_count = 0;
            test_buffer[test_off++] = '\r';
            test_buffer[test_off++] = '\n';
        }
        received_byte_buffer[buffer_index++] = byte_value;
    }

    received_byte_buffer[buffer_index++] = '\r';
    received_byte_buffer[buffer_index++] = '\n';
    received_byte_buffer[buffer_index++] = '\0';

    // Отправка накопленного буфера по завершении приёма
    if (buffer_index > 0) {
        uart_write_bytes(uart_port, received_byte_buffer, buffer_index);
    }
    test_buffer[test_off++] = '\r';
    test_buffer[test_off++] = '\n';
    test_buffer[test_off++] = '\0';
    uart_write_bytes(uart_port, test_buffer, 1024);
}


void test_receive_bin(const uart_port_t uart_port, const int threshold) {
    for (int a = 0; a < 10; ++a) {
        char buffer[98]; // 96 символов + 1 для \n
        int offset = 0;

        for (int i = 0; i < 96; i++) {
            const int adc_reading = adc1_get_raw(ADC1_CHANNEL_4);
            const int signal = adc_reading > threshold ? 1 : 0;
            // buffer[offset++] = signal ? '#' : ' ';
            buffer[offset++] = signal ? '1' : '0';
            ets_delay_us(5);
        }

        buffer[offset++] = '\r'; // Добавляем перенос строки
        buffer[offset++] = '\n'; // Добавляем перенос строки
        uart_write_bytes(uart_port, buffer, offset);
    }
}

static char console_buffer[1024];

void test_receive_all(const uart_port_t uart_port, const int threshold) {
    for (int a = 0; a < 10; ++a) {
        int offset = 0;

        for (int i = 0; i < 96; i++) {
            const int adc_reading = adc1_get_raw(ADC1_CHANNEL_4);
            offset += snprintf(console_buffer + offset, sizeof(console_buffer) - offset, "%lld ", esp_timer_get_time());
            offset += snprintf(console_buffer + offset, sizeof(console_buffer) - offset, "%d ; ",
                               adc_reading > threshold ? 1 : 0);
            console_buffer[offset++] = '\r'; // Добавляем перенос строки
            console_buffer[offset++] = '\n'; // Добавляем перенос строки
        }

        // console_buffer[offset++] = '\r'; // Добавляем перенос строки
        // console_buffer[offset++] = '\n'; // Добавляем перенос строки
        console_buffer[offset++] = '\0'; // Добавляем перенос строки
        uart_write_bytes(uart_port, console_buffer, offset);
    }
}


#define RAW_RECEIVE_ROWS 16

void test_receive_raw(const uart_port_t uart_port) {
    char buffer[4 * RAW_RECEIVE_ROWS + 2];
    int offset = 0;

    for (int i = 0; i < RAW_RECEIVE_ROWS; i++) {
        const int adc_reading = adc1_get_raw(ADC1_CHANNEL_4);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%03d ", adc_reading);
    }
    buffer[4 * RAW_RECEIVE_ROWS] = '\r';
    buffer[4 * RAW_RECEIVE_ROWS + 1] = '\n';

    uart_write_bytes(uart_port, buffer, strlen(buffer));
}

void init_receiver() {
    init_synchronizer();
    for (int i = 0; i < 1024; ++i) {
        console_buffer[i] = 0;
    }
}
