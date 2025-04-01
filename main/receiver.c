#include "receiver.h"

#include <esp_timer.h>
#include <math.h>
#include <rtc.h>
#include <rtc_wdt.h>
#include <stdio.h>
#include <string.h>
#include <utils.h>
#include <driver/adc.h>
#include <driver/uart.h>
#include <rom/ets_sys.h>

#include "synchronizer.h"

static char console_buffer[1024];

void init_receiver() {
    init_synchronizer();
    for (int i = 0; i < 1024; ++i) {
        console_buffer[i] = 0;
    }
}

// Используем отношение среднего значение буфера сканирования по отношению к порогу: выше единицы => выше порога
char decode_manchester_pair(const double first, const double second) {
    if (first < 1 && second >= 1) {
        return 1;
    }
    if (first >= 1 && second < 1) {
        return 0;
    }
    if (fabs(first - second) < 0.05) {
        return 2;
    }
    return second > first ? 1 : 0;
}


#define BUFFER_SIZE 1024
#define MANCHESTER_BITS 17

static uint8_t received_byte_buffer[BUFFER_SIZE];
#define READ_BUFFER_MAX_SIZE 10
static int read_buffer[READ_BUFFER_MAX_SIZE];

void process_manchester_receive(
    const int threshold, const double baseFrequency,
    const uart_port_t uart_port
) {
    if (baseFrequency <= 0) return;
    memset(received_byte_buffer, 0, sizeof(received_byte_buffer));
    if (!await_end_sync(threshold)) {
        // const char* console_buffer = "Sync timeout\r\n\0";
        // uart_write_bytes(uart_port, console_buffer, strlen(console_buffer));
        return;
    }
    // const char* console_buffer = "HANDLED!!!!!!!!\r\n\0";
    // uart_write_bytes(uart_port, console_buffer, strlen(console_buffer));

    int packet_byte_buffer_index = 0;

    double received_byte[MANCHESTER_BITS] = {0};
    int received_byte_bit_index = 0;

    // Расчёт целевого интервала для одного полупериода в микросекундах
    const int max_delay_period_us = (int)(1000000.0 / baseFrequency * 10);
    int half_period_target_us = (int)(1000000.0 / baseFrequency / 2);
    if (half_period_target_us < 1) half_period_target_us = 1;
    const double max_stable_period_us_d = 1500000.0 / baseFrequency / 2;
    int max_stable_period_us;
    if (max_stable_period_us_d < 1) {
        max_stable_period_us = 2;
    } else {
        max_stable_period_us = ceil(max_stable_period_us_d);
    }

    int read_buffer_size = half_period_target_us;
    if (read_buffer_size > READ_BUFFER_MAX_SIZE) {
        read_buffer_size = READ_BUFFER_MAX_SIZE;
    }
    for (int i = 0; i < read_buffer_size; ++i) {
        read_buffer[i] = -1;
    }

    double last_bit = -1;
    int64_t stable_duration_start = esp_timer_get_time();

    // char buf[32];
    // snprintf(buf, sizeof(buf), "Max %d \r\n", max_delay_period_us);
    // uart_write_bytes(UART_NUM_0, buf, strlen(buf));

    while (1) {
        rtc_wdt_feed();
        const int64_t now = esp_timer_get_time();
        shift_left_and_append_int(read_buffer, read_buffer_size, adc1_get_raw(ADC1_CHANNEL_4));
        const double bin_of_buffer = avg_bin_of_buffer(read_buffer, read_buffer_size, threshold);

        if (bin_of_buffer != -1 && (last_bit >= 1) != (bin_of_buffer >= 1)) {
            // if (packet_byte_buffer_index >= BUFFER_SIZE - 3) break;

            // uart_write_bytes(UART_NUM_0, "aaa\r\n", 5);
            if (last_bit != -1) {
                const long long int diff = now - stable_duration_start;

                for (int i = 0; i < (diff > max_stable_period_us ? 2 : 1); ++i) {
                    if (received_byte_bit_index < 17) {
                        received_byte[received_byte_bit_index++] = last_bit;
                    }
                }

                // char aaaa[32];
                // snprintf(aaaa, sizeof(aaaa), "Delay %lld / %d \r\n", diff, max_stable_period_us);
                // uart_write_bytes(UART_NUM_0, aaaa, strlen(aaaa));
            }
            stable_duration_start = now;
            last_bit = bin_of_buffer;

            if (received_byte_bit_index >= 17) {
                // Debug output - show raw Manchester bits first
                // char debug_buf[MANCHESTER_BITS + 1] = {0};
                // for (int i = 0; i < MANCHESTER_BITS; i++) {
                //     debug_buf[i] = received_byte[i] >= 1 ? '1' : '0';
                // }
                // uart_write_bytes(uart_port, "Raw bits: ", 10);
                // uart_write_bytes(uart_port, debug_buf, MANCHESTER_BITS);
                // uart_write_bytes(uart_port, "\r\n\0", 3);


                unsigned char byte_value = 0;
                bool skip_byte = 0;
                for (int i = 0; i < 8; i++) {
                    if (skip_byte) {
                        continue;
                    }
                    const char bit = decode_manchester_pair(
                        received_byte[2 * i], received_byte[2 * i + 1]
                    );
                    if (bit == 2) {
                        skip_byte = true;
                        continue;
                    }
                    byte_value |= (bit << (7 - i));
                    received_byte[2 * i] = 0;
                    received_byte[2 * i + 1] = 0;
                }

                last_bit = -1;
                if (received_byte[16] != -1) {
                    received_byte[0] = received_byte[16];
                    last_bit = received_byte[0];
                    received_byte[16] = -1;
                }

                received_byte_buffer[packet_byte_buffer_index++] = byte_value;
                received_byte_bit_index = 0;
            }
        }

        if ((now - stable_duration_start) > max_delay_period_us) {
            // char aaaa[46];
            // snprintf(aaaa, sizeof(aaaa), "Break %lld / %d \r\n", now - stable_duration_start, max_delay_period_us);
            // uart_write_bytes(UART_NUM_0, aaaa, strlen(aaaa));
            break;
        }
    }

    if (received_byte_bit_index != 0) {
        // char debug_buf[MANCHESTER_BITS + 1] = {0};
        // for (int i = 0; i < MANCHESTER_BITS; i++) {
        //     debug_buf[i] = received_byte[i] >= 1 ? '1' : '0';
        // }
        // uart_write_bytes(uart_port, "Raw bits: ", 10);
        // uart_write_bytes(uart_port, debug_buf, MANCHESTER_BITS);
        // uart_write_bytes(uart_port, "\r\n\0", 3);

        unsigned char byte_value = 0;
        bool skip_byte = 0;
        for (int i = 0; i < 8; i++) {
            if (skip_byte) {
                continue;
            }
            const char bit = decode_manchester_pair(
                received_byte[2 * i], received_byte[2 * i + 1]
            );
            if (bit == 2) {
                skip_byte = true;
                continue;
            }
            byte_value |= (bit << (7 - i));
            received_byte[2 * i] = 0;
            received_byte[2 * i + 1] = 0;
        }
        received_byte_buffer[packet_byte_buffer_index++] = byte_value;
    }

    // received_byte_buffer[packet_byte_buffer_index++] = '\r';
    // received_byte_buffer[packet_byte_buffer_index++] = '\n';
    received_byte_buffer[packet_byte_buffer_index++] = '\0';

    // Отправка накопленного буфера по завершении приёма
    if (packet_byte_buffer_index > 0) {
        uart_write_bytes(uart_port, received_byte_buffer, packet_byte_buffer_index);
    }
}


void test_receive_all(const uart_port_t uart_port, const int threshold) {
    for (int a = 0; a < 10; ++a) {
        char buffer[98]; // 96 символов + 3 для \r\n\0
        int offset = 0;

        for (int i = 0; i < 96; i++) {
            const int adc_reading = adc1_get_raw(ADC1_CHANNEL_4);
            const int signal = adc_reading > threshold ? 1 : 0;
            // buffer[offset++] = signal ? '#' : ' ';
            buffer[offset++] = signal ? '1' : '0';
            ets_delay_us(5);
        }

        buffer[offset++] = '\n'; // Добавляем перенос строки
        buffer[offset++] = '\0'; // Добавляем перенос строки
        uart_write_bytes(uart_port, buffer, offset);
    }
}

// void test_receive_all(const uart_port_t uart_port, const int threshold) {
//     for (int a = 0; a < 10; ++a) {
//         int offset = 0;
//         for (int i = 0; i < 96; i++) {
//             if (offset >= sizeof(console_buffer)) break; // Защита от переполнения
//             const int adc_reading = adc1_get_raw(ADC1_CHANNEL_4);
//             offset += snprintf(console_buffer + offset, sizeof(console_buffer) - offset, "%lld ", esp_timer_get_time());
//             offset += snprintf(console_buffer + offset, sizeof(console_buffer) - offset, "%d ; ", adc_reading > threshold ? 1 : 0);
//             console_buffer[offset++] = '\r';
//             console_buffer[offset++] = '\n';
//         }
//         console_buffer[offset++] = '\0';
//         uart_write_bytes(uart_port, console_buffer, offset);
//     }
// }


#define RAW_RECEIVE_ROWS 16

void test_receive_raw(const uart_port_t uart_port) {
    char buffer[4 * RAW_RECEIVE_ROWS + 2];
    int offset = 0;

    for (int i = 0; i < RAW_RECEIVE_ROWS; i++) {
        const int adc_reading = adc1_get_raw(ADC1_CHANNEL_4);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%03d ", adc_reading);
    }
    buffer[4 * RAW_RECEIVE_ROWS] = '\n';
    buffer[4 * RAW_RECEIVE_ROWS + 1] = '\0';

    uart_write_bytes(uart_port, buffer, 4 * RAW_RECEIVE_ROWS + 2);
}
