#include <esp_log.h>
#include <esp_log_level.h>
#include <esp_task_wdt.h>
#include <receiver.h>
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
#include <driver/adc.h>
#include <rom/ets_sys.h>
#include <rom/gpio.h>
#include <sys/time.h>

// Светодиод на GPIO 17 (TX2 ESP32)
#define LED_GPIO         GPIO_NUM_17

#define UART_PORT_NUM    UART_NUM_0  // UART для связи (USB)
#define BUF_SIZE         1024        // Размер буфера для UART
#define IDLE_TIMEOUT_MS  200         // Таймаут (мс) для определения простоя передачи (приём)

// Максимально возможная частота передачи (в Гц)
#define MAX_FREQ         500000
#define BLINK_TIME_SECS 2

// Глобальная переменная для частоты передачи (битовая частота в Гц)
// Может быть изменена командой вида "#FREQ <значение>"
volatile int frequency = 100;
volatile int blink_frequency = 100;
volatile int threshold = 110;
volatile bool normalRead = 0;
volatile bool rawRead = 0;
volatile bool binRead = 0;
volatile bool readMode = 0;
volatile bool blinkMode = 0;
volatile bool duplexMode = 0;

void found_threshold(void) {
    printf("Scanning min and max value\n");
    int max;
    int min = max = adc1_get_raw(ADC1_CHANNEL_4);
    for (int i = 0; i <= 4096; i++) {
        const int v = adc1_get_raw(ADC1_CHANNEL_4);
        if (max < v) {
            max = v;
        }
        if (min > v) {
            min = v;
        }
        ets_delay_us(1);
    }
    const int middle = (min + max) / 2;
    printf("Min: %d, Max: %d, Middle point: %d\nScanning for AVG low and high\n", min, max, middle);

    long long int sum_low = 0;
    int count_low = 0;
    long long int sum_high = 0;
    int count_high = 0;
    for (int i = 0; i <= 50000; i++) {
        const int v = adc1_get_raw(ADC1_CHANNEL_4);
        if (v < middle) {
            sum_low += v;
            count_low++;
        } else if (v > middle) {
            sum_high += v;
            count_high++;
        }
        ets_delay_us(1);
    }

    const int middle_low = (sum_low / count_low);
    const int middle_high = (sum_high / count_high);
    printf("Low: %d, High: %d\n", middle_low, middle_high);
    threshold = (middle_low + 0.5 * (middle_high - middle_low));
    printf("Set THR to %d\n", threshold);
}

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
                printf("Frequency installed to %d Hz\n", frequency);
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
            if (endptr != arg && new_thr > 0 && new_thr < 4096) {
                threshold = (int)new_thr;
                printf("Threshold installed to %d\n", threshold);
            } else {
                printf("Incorrect threshold: %s\n", arg);
            }
        } else {
            printf("Команда #THR требует аргумент, например: #THR 2\n");
        }
    } else if (strncmp(cmd, "#BLINK", 6) == 0) {
        const char* arg = cmd + 6;
        while (*arg == ' ' || *arg == '\t') {
            arg++;
        }
        if (*arg) {
            char* endptr;
            const double new_blink_freq = strtod(arg, &endptr);
            if (endptr != arg && new_blink_freq > 0 && new_blink_freq <= MAX_FREQ * 2) {
                blink_frequency = (int)new_blink_freq;
                rawRead = 0;
                binRead = 0;
                normalRead = 0;
                readMode = 0;
                blinkMode = 1;
        duplexMode = 0;
                printf("Blinking with %d Hz\n", blink_frequency);
            } else {
                printf("Incorrect frequency: %s\n", arg);
            }
        } else {
            printf("Команда #BLINK требует аргумент, например: #BLINK 2 (частота в Гц от 1 до %d)\n", MAX_FREQ * 2);
        }
    } else if (strncmp(cmd, "#RNOR", 5) == 0) {
        printf("Normal mode\n");
        rawRead = 0;
        binRead = 0;
        normalRead = 1;
        readMode = 1;
        blinkMode = 0;
        duplexMode = 0;
    } else if (strncmp(cmd, "#RRAW", 5) == 0) {
        printf("Raw mode\n");
        rawRead = 1;
        binRead = 0;
        normalRead = 0;
        readMode = 1;
        blinkMode = 0;
        duplexMode = 0;
    } else if (strncmp(cmd, "#RBIN", 5) == 0) {
        printf("Bin mode\n");
        rawRead = 0;
        binRead = 1;
        normalRead = 0;
        readMode = 1;
        blinkMode = 0;
        duplexMode = 0;
    } else if (strncmp(cmd, "#SEND", 5) == 0) {
        printf("Send mode\n");
        rawRead = 0;
        binRead = 0;
        normalRead = 0;
        readMode = 0;
        blinkMode = 0;
        duplexMode = 0;
    } else if (strncmp(cmd, "#DUPL", 5) == 0) {
        printf("Half-Duplex mode\n");
        rawRead = 0;
        binRead = 0;
        normalRead = 0;
        readMode = 0;
        blinkMode = 0;
        duplexMode = 1;
    } else if (strncmp(cmd, "#ATHR", 5) == 0) {
        found_threshold();
    } else {
        printf("Unknown command: %s\n", cmd);
    }
}

#define BASE_BLINK_FREQ 10

// Простая функция непрерывного мигания
void app_main_(void) {
    gpio_pad_select_gpio(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    const int period = 500000 / BASE_BLINK_FREQ;
    while (1) {
        gpio_set_level(LED_GPIO, 1);
        ets_delay_us(period);
        gpio_set_level(LED_GPIO, 0);
        ets_delay_us(period);
        rtc_wdt_feed();
    }
}

void app_main(void) {
    // Настраиваем подключение по COM порту (UART):
    // Ставим БОД, битовые режимы, отключаем все стандартные логи
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
    uart_write_bytes(UART_PORT_NUM, "\n\0", 2);

    // Настройка ширины ADC (12 бит)
    adc1_config_width(ADC_WIDTH_BIT_12);

    // Настройка аттенюации для канала ADC1_CHANNEL_4 (GPIO32)
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_0);

    gpio_pad_select_gpio(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    init_receiver();

    while (1) {
        uint8_t data[BUF_SIZE];
        const TickType_t waitTicks = (duplexMode || readMode || blinkMode) ? 0 : pdMS_TO_TICKS(100);
        const int len = uart_read_bytes(UART_PORT_NUM, data, BUF_SIZE, waitTicks);

        if (len > 0) {
            if (data[0] == '#' && len < 100) {
                // Обработка команды
                char cmd[BUF_SIZE];
                memcpy(cmd, data, len);
                cmd[len] = '\0';
                process_command(cmd);
            } else if (!readMode || duplexMode) {
                process_binary_data(data, len, frequency);
            }
        }
        if (blinkMode) {
            const int period = 500000 / blink_frequency;
            // BLINK_TIME_SECS секунд непрерывных Blink морганий, прежде чем сделаем попытку ввода с консоли
            // for (int i = 0; i < (BLINK_TIME_SECS * 1000000 / period); ++i) {
            gpio_set_level(LED_GPIO, 1);
            ets_delay_us(period);
            gpio_set_level(LED_GPIO, 0);
            ets_delay_us(period);
            // }
        }
        // Режимы чтения
        if (readMode || duplexMode) {
            if (normalRead || duplexMode) {
                // Попытка приёма кодированной информации
                process_manchester_receive(threshold, frequency, UART_PORT_NUM);
            } else if (rawRead && !duplexMode) {
                // Режим аналогового чтения
                test_receive_raw(UART_PORT_NUM);
                ets_delay_us(10);
            } else if (binRead && !duplexMode) {
                // Режим бинарного чтения
                test_receive_all(UART_PORT_NUM, threshold);
                ets_delay_us(10);
            }
        }

        // Сбрасываем ("кормим") Watchdog таймер, чтобы не было принудительного завершения программы
        rtc_wdt_feed();
    }
}
