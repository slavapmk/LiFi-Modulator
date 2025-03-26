#include <esp_log.h>
#include <esp_log_level.h>
#include <esp_task_wdt.h>
#include <reciever.h>
#include <rtc_wdt.h>
#include <sender.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// #include <driver/adc.h>
#include <driver/adc.h>
#include <hal/wdt_hal.h>
#include <rom/ets_sys.h>
#include <rom/gpio.h>
#include <sys/time.h>

#define READ_MODE 1

// Светодиод на GPIO 17 (TX ESP32)
#define PHOTO_GPIO       16         // Фотодиод на GPIO 16 (RX ESP32)
#define UART_PORT_NUM    UART_NUM_0  // UART для связи (USB)
#define BUF_SIZE         1024        // Размер буфера для UART
#define IDLE_TIMEOUT_MS  200         // Таймаут (мс) для определения простоя передачи (приём)

// Максимально возможная частота передачи (в Гц)
#define MAX_FREQ         500000

// Глобальная переменная для частоты передачи (битовая частота в Гц)
// Может быть изменена командой вида "#FREQ <значение>"
volatile double frequency = 100.0;
volatile int threshold = 110;
volatile bool normalRead = 1;
volatile bool rawRead = 0;
volatile bool binRead = 0;
volatile bool readMode = 0;

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
            const double new_freq = strtod(arg, &endptr);
            if (endptr != arg && new_freq > 0 && new_freq <= MAX_FREQ) {
                frequency = new_freq;
                printf("Frequency installed to %.2f Hz\n", frequency);
            } else {
                printf("Incorrect frequency: %s (mac: %d Hz)\n", arg, MAX_FREQ);
            }
        } else {
            printf("Команда #FREQ требует аргумент, например: #FREQ 2\n");
        }
    } else if (strncmp(cmd, "#THR", 4) == 0) {
        const char* arg = cmd + 4;
        while (*arg == ' ' || *arg == '\t') {
            arg++;
        }
        if (*arg) {
            char* endptr;
            const double new_thr = strtod(arg, &endptr);
            if (endptr != arg && new_thr > 0) {
                threshold = new_thr;
                printf("Threshold installed to %d\n", threshold);
            } else {
                printf("Incorrect threshold: %s\n", arg);
            }
        } else {
            printf("Команда #THR требует аргумент, например: #THR 2\n");
        }
    } else if (strncmp(cmd, "#RNOR", 5) == 0) {
        printf("Normal mode\n");
        rawRead = 0;
        binRead = 0;
        normalRead = 1;
        readMode = 1;
    } else if (strncmp(cmd, "#RRAW", 5) == 0) {
        printf("Raw mode\n");
        rawRead = 1;
        binRead = 0;
        normalRead = 0;
        readMode = 1;
    } else if (strncmp(cmd, "#RBIN", 5) == 0) {
        printf("Bin mode\n");
        rawRead = 0;
        binRead = 1;
        normalRead = 0;
        readMode = 1;
    } else if (strncmp(cmd, "#SEND", 5) == 0) {
        printf("Send mode\n");
        rawRead = 0;
        binRead = 0;
        normalRead = 0;
        readMode = 0;
    } else {
        printf("Unknown command: %s\n", cmd);
    }
}

//
// Функция передачи одного бита с манчестерским кодированием.
// Для битового периода T = 1/frequency, каждая половина длится T/2 мкс.
// Схема: для бита 0 – высокий уровень, затем низкий; для бита 1 – низкий уровень, затем высокий.
//

//
// Функция передачи массива данных (байт) с манчестерским кодированием.
// Данные передаются от старшего к младшему биту каждого байта.
//

//
// void test_recieve_all(void) {
//     int raw;
//     adc2_get_raw(ADC2_CHANNEL_0, ADC_WIDTH_BIT_12, &raw);
//     printf("ADC2 Value: %d\n", raw);
//
//     // for (int i = 0; i < 32; i++) {
//     //     const int bit = gpio_get_level(PHOTO_GPIO); // Получаем 0 или 1
//     //     printf("%d", bit);
//     // }
//     // printf("\n");
//     // uint32_t data = 0;
//     //
//     // for (int i = 0; i < 32; i++) {
//     //     const int bit = gpio_get_level(PHOTO_GPIO); // Получаем 0 или 1
//     //     data = (data << 1) | (bit & 1); // Сдвигаем влево и добавляем бит
//     // }
//     //
//     // printf("%02X %02X %02X %02X\n",
//     //        (unsigned int)((data >> 24) & 0xFF),
//     //        (unsigned int)((data >> 16) & 0xFF),
//     //        (unsigned int)((data >> 8) & 0xFF),
//     //        (unsigned int)(data & 0xFF));
// }
//
// //
// // Основной цикл приложения.
// // Если по UART поступают данные, они обрабатываются как команды или передаются с манчестерским кодированием через светодиод.
// // Если данные по UART отсутствуют, система переходит в режим приёма через фотодиод.
// //
// void app_main(void) {
//     rtc_wdt_protect_off(); // Turns off the automatic wdt service
//     rtc_wdt_enable(); // Turn it on manually
//     rtc_wdt_set_time(RTC_WDT_STAGE0, 20000); // Define how long you desire to let dog wait.
//
//     gpio_pad_select_gpio(LED_GPIO);
//     gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
//     gpio_set_level(LED_GPIO, 0);
//
//     gpio_pad_select_gpio(PHOTO_GPIO);
//     gpio_set_direction(PHOTO_GPIO, GPIO_MODE_INPUT);
//
//     // Инициализация Watchdog с использованием структуры конфигурации.
//     // const esp_task_wdt_config_t wdt_config = {
//     //     .timeout_ms = 30000,
//     //     .trigger_panic = false
//     // };
//     // esp_task_wdt_init(&wdt_config);
//     // esp_task_wdt_add(NULL); // Регистрируем текущую задачу
//
//     const uart_config_t uart_config = {
//         .baud_rate = 115200,
//         .data_bits = UART_DATA_8_BITS,
//         .parity = UART_PARITY_DISABLE,
//         .stop_bits = UART_STOP_BITS_1,
//         .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
//     };
//     // esp_log_level_set("*", ESP_LOG_NONE);
//     uart_param_config(UART_PORT_NUM, &uart_config);
//     uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
//
//     while (1) {
//         test_recieve_all();
//         // process_manchester_receive();
//         vTaskDelay(100 / portTICK_PERIOD_MS);
//     }
// }

void app_main(void) {
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    esp_log_level_set("*", ESP_LOG_NONE);
    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    printf("\r\n");

    // Настройка ширины ADC (12 бит)
    adc1_config_width(ADC_WIDTH_BIT_12);

    // Настройка аттенюации для канала ADC1_CHANNEL_4 (GPIO32)
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_12);

    while (1) {
        uint8_t data[BUF_SIZE];
        const TickType_t waitTicks = readMode ? 1 : 20 / portTICK_PERIOD_MS;
        const int len = uart_read_bytes(UART_PORT_NUM, data, BUF_SIZE, waitTicks);

        if (len > 0) {
            if (data[0] == '#' && len < 100) {
                // Обработка команды
                char cmd[BUF_SIZE];
                memcpy(cmd, data, len);
                cmd[len] = '\0';
                process_command(cmd);
            } else if (!readMode) {
                process_binary_data(data, len, frequency);
            }
        }
        if (readMode) {
            if (normalRead) {
                process_manchester_receive(threshold, frequency, UART_PORT_NUM);
            } else if (rawRead) {
                test_recieve_raw(UART_PORT_NUM);
                ets_delay_us(1);
            } else if (binRead) {
                test_recieve_all(UART_PORT_NUM, threshold);
                ets_delay_us(1);
            }
        }
        rtc_wdt_feed();
    }
}
