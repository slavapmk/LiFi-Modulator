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
    // if (fabs(first - second) < 0.02) {
    //     return 2;
    // }
    return second > first ? 1 : 0;
}


#define BUFFER_SIZE 1024
#define MAX_PACKER_SIZE 128
#define AVG_ATT 50

static uint8_t received_bytes_buffer[BUFFER_SIZE];
static double received_half_bits_buffer[BUFFER_SIZE * 8 * 2];

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

void process_manchester_receive(
    const int threshold, const int baseFrequency,
    const uart_port_t uart_port
) {
    if (baseFrequency <= 0) return;
    memset(received_bytes_buffer, 0, sizeof(received_bytes_buffer));
    memset(received_half_bits_buffer, 0, sizeof(received_half_bits_buffer));
    if (!await_end_sync(threshold)) {
        return;
    }

    int packet_byte_buffer_index = 0;

    int received_byte_bit_index = 0;

    // Расчёт целевого интервала для одного полупериода в микросекундах
    const double max_delay_period_us = 5000000.0 / baseFrequency;
    double half_period_target_us = 500000.0 / baseFrequency;
    if (half_period_target_us < 1) half_period_target_us = 1;
    const double max_stable_period_us_d = 750000.0 / baseFrequency;

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
            if (last_bit != -1) {
                const double diff = (now - stable_duration_start);

                for (int i = 0; i < (diff > max_stable_period_us_d ? 2 : 1); ++i) {
                    received_half_bits_buffer[received_byte_bit_index++] = last_bit;
                }
            }
            stable_duration_start = now;
            last_bit = bin_of_buffer;
        }

        if (
            (now - stable_duration_start) > max_delay_period_us ||
            received_byte_bit_index / 16 >= MAX_PACKER_SIZE
        ) {
            break;
        }
    }

    for (int byte_bits_index = 0; byte_bits_index <= received_byte_bit_index / 16; ++byte_bits_index) {
        unsigned char byte_value = 0;
        bool skip_byte = 0;
        for (int i = 0; i < 8; ++i) {
            if (skip_byte) {
                continue;
            }
            const double first = received_half_bits_buffer[byte_bits_index * 16 + i * 2];
            const double second = received_half_bits_buffer[byte_bits_index * 16 + i * 2 + 1];
            // printf("%f %f\n", first, second);
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
        received_bytes_buffer[packet_byte_buffer_index++] = byte_value;
    }

    // print_double_arraqy(bits_buffer, received_byte_bit_index);
    print_double_arraqy(received_half_bits_buffer, received_byte_bit_index);

    // Отправка накопленного буфера по завершению приёма
    if (packet_byte_buffer_index > 0) {
        received_bytes_buffer[packet_byte_buffer_index++] = '\r';
        received_bytes_buffer[packet_byte_buffer_index++] = '\n';
        received_bytes_buffer[packet_byte_buffer_index++] = '\0';
        uart_write_bytes(uart_port, received_bytes_buffer, packet_byte_buffer_index);
    }

    printf(
        "max_delay_period_us %f\nhalf_period_target_us %f\nmax_stable_period_us_d %f\n\n",
        max_delay_period_us,
        half_period_target_us,
        max_stable_period_us_d
    );
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
