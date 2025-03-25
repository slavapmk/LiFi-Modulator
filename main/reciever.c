#include "reciever.h"

#include <esp_timer.h>
#include <stdio.h>
#include <driver/adc.h>
#include <driver/uart.h>
#include <rom/ets_sys.h>

#define BUFFER_SIZE 128
void process_manchester_receive(
    const int threshold, const double baseFrequency,
    uart_port_t uart_port
) {
    uint8_t buffer[BUFFER_SIZE];
    int buffer_index = 0;
    uint8_t received_byte = 0;
    int bit_count = 0;

    // Флаг, показывающий, что стартовая последовательность (10) успешно проверена
    bool start_sequence_checked = false;

    // Расчёт целевого интервала для одного полупериода в микросекундах
    int max_delay_period_us = (int)(1000000.0 / baseFrequency * 10);
    int half_period_target_us = (int)(1000000.0 / baseFrequency / 2);
    if (half_period_target_us < 1) half_period_target_us = 1;

    int sync_state = 0; // Состояние синхронизации
    uint64_t last_signal_time = esp_timer_get_time(); // Время последнего изменения сигнала
    uint64_t period_start = 0; // Отсчёт интервала для формирования бита

    while (1) {
        uint64_t now = esp_timer_get_time();
        int adc_reading = adc1_get_raw(ADC1_CHANNEL_4);
        int signal = (adc_reading > threshold) ? 1 : 0;

        if (sync_state == 0) {
            // Поиск первого фронта для синхронизации
            if (signal) {
                sync_state = 1;
                period_start = now;
            }
        } else if (sync_state == 1) {
            // Завершаем первый полупериод
            if (!signal) {
                sync_state = 2;
                last_signal_time = now;
            }
        } else {
            // Принимаем биты через каждые half_period_target_us микросекунд
            if (now - period_start >= (uint64_t)half_period_target_us) {
                period_start += half_period_target_us;

                // Чтение текущего состояния сигнала
                if (signal) {
                    last_signal_time = now;
                    received_byte = (received_byte << 1) | 1;
                } else {
                    received_byte = (received_byte << 1);
                }
                bit_count++;

                // Проверка стартовой последовательности в первых двух битах
                if (!start_sequence_checked && bit_count == 2) {
                    // Проверяем, что первые два бита равны 10
                    if ((received_byte & 0x03) != 0x02) {
                        // Если не совпадает, сбрасываем приём и возвращаемся к синхронизации
                        bit_count = 0;
                        received_byte = 0;
                        sync_state = 0;
                    } else {
                        // Если стартовая последовательность корректна, оставляем её в данных
                        start_sequence_checked = true;
                    }
                }

                // Формируем байт по 8 бит
                if (bit_count == 8) {
                    if (buffer_index < BUFFER_SIZE) {
                        buffer[buffer_index++] = received_byte;
                    } else {
                        // Переполнение буфера
                        break;
                    }
                    bit_count = 0;
                    received_byte = 0;
                    // Если требуется, можно сбрасывать флаг start_sequence_checked для каждого нового пакета,
                    // но если стартовая последовательность передаётся лишь в начале, флаг остаётся установленным.
                }
            }
        }

        // Выход по таймауту, если сигнал не изменялся
        if (now - last_signal_time > max_delay_period_us) {
            break;
        }

        // Задержка 1 мкс для непрерывного опроса
        ets_delay_us(1);
    }

    // Отправка накопленного буфера по завершении приёма
    if (buffer_index > 0) {
        printf("\n");
        uart_write_bytes(uart_port, buffer, buffer_index);
        printf("\n");
    }
}

void test_recieve_all(const int threshold) {
    for (int i = 0; i < 32; i++) {
        const int adc_reading = adc1_get_raw(ADC1_CHANNEL_4);
        const int signal = adc_reading > threshold ? 1 : 0;
        printf("%d", signal);
    }
    printf("\n");
}
