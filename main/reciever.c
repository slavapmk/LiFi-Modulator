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

    // Переменные для формирования декодированного байта
    uint8_t decoded_byte = 0;
    int decoded_bit_count = 0;

    // Переменные для накопления двух полубитов (каждый интервал)
    uint8_t raw_bits = 0;
    int raw_bit_count = 0;

    // Расчёт целевого интервала для одного полупериода (в микросекундах)
    int max_delay_period_us = (int)(1000000.0 / baseFrequency * 10);
    int half_period_target_us = (int)(1000000.0 / baseFrequency / 2);
    if (half_period_target_us < 1) half_period_target_us = 1;

    int sync_state = 0; // Состояние синхронизации
    uint64_t last_signal_time = esp_timer_get_time(); // Время последнего изменения сигнала
    uint64_t period_start = 0; // Отсчёт интервала для формирования полубита

    // Для отслеживания первой синхронизирующей последовательности
    uint8_t sync_sequence = 0b0110; // Начальная синхронизирующая последовательность (манчестерский код для 10)
    int sync_bit_count = 0;

    while (1) {
        uint64_t now = esp_timer_get_time();
        int adc_reading = adc1_get_raw(ADC1_CHANNEL_4);
        int signal = (adc_reading > threshold) ? 1 : 0;

        // Проверка на изменение сигнала (для синхронизации)
        if (signal != (last_signal_time > 0 ? (last_signal_time % 2) : 0)) {
            last_signal_time = now;

            if (sync_state == 0) {
                // Ожидаем первую синхронизирующую последовательность 0110
                if (((raw_bits << 1) | signal) == sync_sequence) {
                    sync_bit_count++;
                    raw_bits = (raw_bits << 1) | signal;
                    if (sync_bit_count == 4) {
                        // После получения 4 бит синхронизации начинаем прием
                        sync_state = 1;
                        raw_bits = 0;
                        raw_bit_count = 0;
                        decoded_bit_count = 0;
                        decoded_byte = 0;
                    }
                } else {
                    raw_bits = (raw_bits << 1) | signal;
                    raw_bit_count++;
                    if (raw_bit_count > 4) {
                        // Сбрасываем если последовательность не совпала
                        raw_bits = 0;
                        raw_bit_count = 0;
                    }
                }
            }
            // Если мы в состоянии синхронизации, принимаем данные
            else if (sync_state == 1) {
                // Принимаем полубит каждые half_period_target_us микросекунд
                if (now - period_start >= (uint64_t)half_period_target_us) {
                    period_start += half_period_target_us;

                    // Сдвигаем накопление полубитов и добавляем текущий сигнал (1 или 0)
                    raw_bits = (raw_bits << 1) | (signal ? 1 : 0);
                    raw_bit_count++;

                    // Когда накоплено 2 полубита, декодируем их как один логический бит
                    if (raw_bit_count == 2) {
                        int logical_bit = -1;
                        // По поправке: если пара равна 01, то это логическая 1, если 10 — логическая 0
                        if (raw_bits == 0b01) {
                            logical_bit = 1;
                        } else if (raw_bits == 0b10) {
                            logical_bit = 0;
                        } else {
                            // Если пара некорректна (00 или 11), сбрасываем накопление и возвращаемся к синхронизации
                            raw_bit_count = 0;
                            raw_bits = 0;
                            sync_state = 0;
                            continue;
                        }

                        // Добавляем декодированный бит в сформированный байт
                        decoded_byte = (decoded_byte << 1) | logical_bit;
                        decoded_bit_count++;

                        // Сбрасываем переменные для следующей пары
                        raw_bit_count = 0;
                        raw_bits = 0;

                        // Если накоплено 8 декодированных бит, сохраняем байт в буфер
                        if (decoded_bit_count == 8) {
                            if (buffer_index < BUFFER_SIZE) {
                                buffer[buffer_index++] = decoded_byte;
                            } else {
                                // Переполнение буфера
                                break;
                            }
                            decoded_bit_count = 0;
                            decoded_byte = 0;
                        }
                    }

                    last_signal_time = now;
                }
            }
        }

        // Выход по таймауту, если сигнал не изменялся
        if (now - last_signal_time > max_delay_period_us) {
            break;
        }

        // Задержка для почти непрерывного опроса
        ets_delay_us(1);
    }

    // Отправка накопленного буфера по завершении приёма
    if (buffer_index > 0) {
        printf("\r\n");
        uart_write_bytes(uart_port, buffer, buffer_index);
        printf("\r\n");
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
