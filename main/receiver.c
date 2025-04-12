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
    if (fabs(first - second) < 0.02) {
        return 2;
    }
    return second > first ? 1 : 0;
}

#define CYCLE_BUFFER_SIZE 1

#define MAX_BYTES 1024
#define MAX_HALF_BITS 16384

void print_double_arraqy(double arr[], const int size) {
    int offset = 0;
    for (int i = 0; i < size; i++) {
        offset += snprintf(console_buffer + offset, sizeof(console_buffer) - offset, "%.2f ", arr[i]);
    }
    console_buffer[offset] = '\r'; // Перевод строки с помощью \r\n
    console_buffer[offset + 1] = '\n';
    console_buffer[offset + 2] = '\0'; // Завершающий нулевой символ

    uart_write_bytes(UART_NUM_0, console_buffer, strlen(console_buffer));
}

int read_avg_samples(const int samples, const int delay_us) {
    int sum = 0;
    for (int i = 0; i < samples; ++i) {
        sum += adc1_get_raw(ADC1_CHANNEL_4);
        if (delay_us > 0) {
            ets_delay_us(delay_us);
        }
    }
    return sum / samples;
}

static int read_buffer[CYCLE_BUFFER_SIZE] = {0};
static double half_bits_buffer[MAX_HALF_BITS] = {0};
static unsigned char bytes_buffer[MAX_BYTES] = {0};
static double delays[256] = {0};
void process_manchester_receive(
    const int threshold, const int baseFrequency,
    const uart_port_t uart_port
) {
    memset(read_buffer, 0, sizeof(read_buffer));
    memset(half_bits_buffer, 0, sizeof(half_bits_buffer));
    memset(bytes_buffer, 0, sizeof(bytes_buffer));
    memset(delays, 0, sizeof(delays));
    if (!await_end_sync(threshold)) {
        return;
    }
    int read_index = 0;

    // Буфер для хранения принятых битов
    int half_bits = 0;

    int last_raw = -1;
    double last_value = -1;
    int64_t stable_start = esp_timer_get_time();

    const double max_delay_period_us = 5000000.0 / baseFrequency;
    const double half_period_target_us = 500000.0 / baseFrequency;
    const double max_stable_period_us_d = 750000.0 / baseFrequency;
    int delay = 0;

    while (true) {
        rtc_wdt_feed();
        const int64_t now = esp_timer_get_time();
        const double diff = (now - stable_start);

        // const int new_value = read_avg_samples(10, 1);
        const int new_value = read_avg_samples(10, 0);
        read_buffer[read_index] = new_value;
        read_index = (read_index + 1) % CYCLE_BUFFER_SIZE;

        const int median = calc_median(read_buffer, CYCLE_BUFFER_SIZE);
        // const int median = 4;

        const double binary = median * 1.0 / threshold;
        if (
            abs(median - last_raw) > 300 &&
            (binary >= 1) != (last_value >= 1)
        ) {
            if (last_value != -1) {
                for (int i = 0; i < (diff > max_stable_period_us_d ? 2 : 1); ++i) {
                    half_bits_buffer[half_bits++] = last_value;
                }
                delays[delay++] = diff;
            }

            last_value = binary;
            last_raw = median;
            stable_start = now;
        }

        if (diff >= max_delay_period_us) {
            break;
        }
        // ets_delay_us(1);
    }

    int packet_byte_buffer_index = 0;
    for (int byte_bits_index = 0; byte_bits_index <= half_bits / 16; ++byte_bits_index) {
        unsigned char byte_value = 0;
        bool skip_byte = 0;
        for (int i = 0; i < 8; ++i) {
            if (skip_byte) {
                continue;
            }
            const double first = half_bits_buffer[byte_bits_index * 16 + i * 2];
            const double second = half_bits_buffer[byte_bits_index * 16 + i * 2 + 1];
            const char bit = decode_manchester_pair(
                first,
                second
            );
            if (bit == 2) {
                skip_byte = true;
                byte_value = ' ';
                continue;
            }
            byte_value |= (bit << (7 - i));
        }
        bytes_buffer[packet_byte_buffer_index++] = byte_value;
    }

    if (packet_byte_buffer_index > 0) {
        bytes_buffer[packet_byte_buffer_index++] = '\r';
        bytes_buffer[packet_byte_buffer_index++] = '\n';
        bytes_buffer[packet_byte_buffer_index++] = '\0';
        uart_write_bytes(uart_port, bytes_buffer, packet_byte_buffer_index);
    }

    print_double_arraqy(half_bits_buffer, half_bits);
    delays[delay++] = max_stable_period_us_d;
    print_double_arraqy(delays, delay);
}


void test_receive_all(const uart_port_t uart_port, const int threshold) {
    for (int a = 0; a < 10; ++a) {
        char buffer[98]; // 96 символов + 3 для \r\n\0
        int offset = 0;

        for (int i = 0; i < 96; i++) {
            const int adc_reading = adc1_get_raw(ADC1_CHANNEL_4);
            const int signal = adc_reading > threshold ? 1 : 0;
            buffer[offset++] = signal ? '#' : ' ';
            // buffer[offset++] = signal ? '1' : '0';
            ets_delay_us(5);
        }

        buffer[offset++] = '\n'; // Добавляем перенос строки
        buffer[offset++] = '\0'; // Добавляем перенос строки
        uart_write_bytes(uart_port, buffer, offset);
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
    buffer[4 * RAW_RECEIVE_ROWS] = '\n';
    buffer[4 * RAW_RECEIVE_ROWS + 1] = '\0';

    uart_write_bytes(uart_port, buffer, 4 * RAW_RECEIVE_ROWS + 2);
}
