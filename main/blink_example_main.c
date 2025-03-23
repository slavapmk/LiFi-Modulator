#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rom/ets_sys.h>
#include <rom/gpio.h>

#define LED_GPIO         17         // Светодиод на GPIO 17 (TX ESP32)
#define PHOTO_GPIO       16         // Фотодиод на GPIO 16 (RX ESP32)
#define UART_PORT_NUM    UART_NUM_0  // UART для связи (USB)
#define BUF_SIZE         1024        // Размер буфера для UART
#define IDLE_TIMEOUT_MS  200         // Таймаут (мс) для определения простоя передачи (приём)

// Максимально возможная частота передачи (в Гц)
#define MAX_FREQ         500000

//
// Глобальная переменная для частоты передачи (битовая частота в Гц)
// Может быть изменена командой вида "#FREQ <значение>"
// При использовании частоты до 1 МГц битовый период будет 1 мкс, что – предельно для бит-бэнга
//
volatile double frequency = 2.0;

//
// Функция обработки команд, например, смены частоты передачи.
//
void process_command(const char* cmd) {
    if (strncmp(cmd, "#FREQ", 5) == 0) {
        const char* arg = cmd + 5;
        while (*arg == ' ' || *arg == '\t') {
            arg++;
        }
        if (*arg) {
            char* endptr;
            double new_freq = strtod(arg, &endptr);
            if (endptr != arg && new_freq > 0 && new_freq <= MAX_FREQ) {
                frequency = new_freq;
                printf("Частота передачи изменена на %.2f Гц\n", frequency);
            } else {
                printf("Неверное значение частоты: %s (макс: %d Гц)\n", arg, MAX_FREQ);
            }
        } else {
            printf("Команда #FREQ требует аргумент, например: #FREQ 2\n");
        }
    } else {
        printf("Неизвестная команда: %s\n", cmd);
    }
}

//
// Функция передачи одного бита с манчестерским кодированием.
// Для битового периода T = 1/frequency, каждая половина длится T/2 мкс.
// Схема: для бита 0 – высокий уровень, затем низкий; для бита 1 – низкий уровень, затем высокий.
//
void send_manchester_bit(int bit) {
    // Расчёт задержки в микросекундах для половины битового периода.
    // При высокой частоте (например, 1 МГц) половина периода будет 0.5 мкс,
    // но ets_delay_us() принимает целые микросекунды – это ограничение.
    int half_period_us = (int)((1000000.0 / frequency) / 2);
    // Если частота очень высокая, half_period_us может получиться равным 0.
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

//
// Функция передачи массива данных (байт) с манчестерским кодированием.
// Данные передаются от старшего к младшему биту каждого байта.
//
void process_binary_data(const uint8_t* data, int len) {
    for (int i = 0; i < len; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            int current = (data[i] >> bit) & 1;
            send_manchester_bit(current);
        }
    }
    // По окончании передачи устанавливаем низкий уровень на выходе.
    gpio_set_level(LED_GPIO, 0);
}

//
// Функция приёма одного бита по фотодиоду с использованием фиксированного периода,
// определяемого переменной frequency (для передачи). Здесь схема определения бита упрощена.
// В реальных условиях при высоких частотах рекомендуется использовать аппаратные таймеры.
//
int receive_manchester_bit() {
    int half_period_us = (int)((1000000.0 / frequency) / 2);
    if (half_period_us < 1) half_period_us = 1;
    // Ожидаем изменения уровня; считаем, что в состоянии покоя уровень равен 0.
    while (gpio_get_level(PHOTO_GPIO) == 0) {
        ets_delay_us(1);
    }
    int first_level = gpio_get_level(PHOTO_GPIO);
    ets_delay_us(half_period_us);
    int mid_level = gpio_get_level(PHOTO_GPIO);
    int bit = -1;
    // Пример интерпретации: если первый уровень 0 и спустя полупериода уровень также 0, считаем бит 1; иначе – бит 0.
    if (first_level == 0 && mid_level == 0)
        bit = 1;
    else if (first_level == 1 && mid_level == 1)
        bit = 0;
    ets_delay_us(half_period_us);
    return bit;
}

//
// Функция приёма манчестерского сигнала по фотодиоду.
// Если по UART нет передачи, система переходит в режим приёма.
// Сначала ожидается появление стартовой последовательности "10",
// затем накапливаются биты до появления простоя, после чего данные группируются в байты
// и отправляются по UART.
//
void process_manchester_receive() {
    printf("Приём по фотодиоду...\n");
    // Динамический буфер для накопления битов
    int capacity = 128;
    int count = 0;
    int *bits = malloc(capacity * sizeof(int));
    if (!bits) {
        printf("Ошибка выделения памяти\n");
        return;
    }

    // Ожидаем появление стартовой последовательности "10"
    int preamble[2] = {-1, -1};
    while (1) {
        int bit = receive_manchester_bit();
        preamble[0] = preamble[1];
        preamble[1] = bit;
        if (preamble[0] == 1 && preamble[1] == 0) {
            // Сохраняем префикс "10"
            bits[count++] = 1;
            bits[count++] = 0;
            printf("Обнаружена преамбула \"10\", начинаем приём данных\n");
            break;
        }
    }

    // Накопление битов до возникновения простоя (IDLE_TIMEOUT_MS)
    TickType_t last_tick = xTaskGetTickCount();
    while ((xTaskGetTickCount() - last_tick) < pdMS_TO_TICKS(IDLE_TIMEOUT_MS)) {
        int bit = receive_manchester_bit();
        if (bit >= 0) {
            if (count >= capacity) {
                capacity *= 2;
                int *temp = realloc(bits, capacity * sizeof(int));
                if (!temp) {
                    printf("Ошибка перераспределения памяти\n");
                    free(bits);
                    return;
                }
                bits = temp;
            }
            bits[count++] = bit;
            last_tick = xTaskGetTickCount();
        }
    }

    // Группировка накопленных битов в байты
    int num_bytes = count / 8;
    if (num_bytes > 0) {
        uint8_t *decoded = malloc(num_bytes);
        if (!decoded) {
            printf("Ошибка выделения памяти для декодирования\n");
            free(bits);
            return;
        }
        for (int i = 0; i < num_bytes; i++) {
            uint8_t byte = 0;
            for (int j = 0; j < 8; j++) {
                byte = (byte << 1) | bits[i * 8 + j];
            }
            decoded[i] = byte;
        }
        // Отправка полученных данных по UART
        printf("Приём по фотодиоду. Получено: (%d бит, %d байт)\n", count, num_bytes);
        uart_write_bytes(UART_PORT_NUM, (const char*)decoded, num_bytes);
        free(decoded);
    } else {
        printf("Недостаточно данных для формирования байта\n");
    }
    free(bits);
}

//
// Основной цикл приложения.
// Если по UART поступают данные, они обрабатываются как команды или передаются с манчестерским кодированием через светодиод.
// Если данные по UART отсутствуют, система переходит в режим приёма через фотодиод.
//
void app_main(void) {
    // Настройка пина для светодиода (выход)
    gpio_pad_select_gpio(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);

    // Настройка пина для фотодиода (вход)
    gpio_pad_select_gpio(PHOTO_GPIO);
    gpio_set_direction(PHOTO_GPIO, GPIO_MODE_INPUT);

    // Настройка UART
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);

    uint8_t data[BUF_SIZE];

    while (1) {
        // Чтение данных с UART (режим передачи)
        const int len = uart_read_bytes(UART_PORT_NUM, data, BUF_SIZE, 20 / portTICK_PERIOD_MS);
        if (len > 0) {
            if (data[0] == '#' && len < 100) {
                // Обработка команды
                char cmd[BUF_SIZE];
                memcpy(cmd, data, len);
                cmd[len] = '\0';
                process_command(cmd);
            } else {
                // Передача данных с манчестерским кодированием через светодиод
                process_binary_data(data, len);
            }
        } else {
            // Если данных по UART нет, переходим в режим приёма через фотодиод
            process_manchester_receive();
        }
    }
}
