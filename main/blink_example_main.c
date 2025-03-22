#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <rom/gpio.h>

#define LED_GPIO         2         // Пин для светодиода
#define UART_PORT_NUM    UART_NUM_0  // UART для связи (обычно USB)
#define BUF_SIZE         1024      // Размер буфера для приёма данных

// Глобальная переменная для частоты (в Гц)
volatile double frequency = 2.0;

// Функция проверки входа
bool check_input() {
    return true; // Заглушка, всегда возвращает true
}

// Функция обработки входного режима
void ensure_input_mode() {
    printf("Режим входа активирован\n");
    // Здесь можно добавить дополнительную логику
}

// Функция обработки специальных команд
void process_command(const char* cmd) {
    if (strncmp(cmd, "#FREQ", 5) == 0) {
        const char* arg = cmd + 5;
        while (*arg == ' ' || *arg == '\t') {
            arg++;
        }
        if (*arg) {
            char* endptr;
            double new_freq = strtod(arg, &endptr);
            if (endptr != arg && new_freq > 0) {
                frequency = (float)new_freq;
                printf("Частота изменена на %.2f Гц\n", frequency);
            }
            else {
                printf("Неверное значение частоты: %s\n", arg);
            }
        }
        else {
            printf("Команда #FREQ требует аргумент, например: #FREQ 2\n");
        }
    }
    else {
        printf("Неизвестная команда: %s\n", cmd);
    }
}

void process_binary_data(const uint8_t* data, const int len) {
    const int total_bits = len * 8;
    int* bits = malloc(total_bits * sizeof(int));
    if (!bits) {
        printf("Ошибка выделения памяти\n");
        return;
    }
    int index = 0;
    for (int i = 0; i < len; i++) {
        for (int bit = 7; bit >= 0; bit--)
        {
            bits[index++] = (data[i] >> bit) & 1;
        }
    }
    int i = 0;
    while (i < total_bits) {
        int current = bits[i];
        int count = 1;
        while ((i + count) < total_bits && bits[i + count] == current)
        {
            count++;
        }
        int delay_ms = (int)(1000.0 / frequency);
        for (int p = 0; p < count; p++)
        {
            gpio_set_level(LED_GPIO, current);
            vTaskDelay(delay_ms / portTICK_PERIOD_MS);
        }
        i += count;
    }
    gpio_set_level(LED_GPIO, 0);
    free(bits);
}

void app_main(void) {
    gpio_pad_select_gpio(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);

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
        if (check_input()) {
            ensure_input_mode();
        }

        int len = uart_read_bytes(UART_PORT_NUM, data, BUF_SIZE, 20 / portTICK_PERIOD_MS);
        if (len > 0)
        {
            if (data[0] == '#' && len < 100)
            {
                char cmd[BUF_SIZE];
                memcpy(cmd, data, len);
                cmd[len] = '\0';
                process_command(cmd);
            }
            else
            {
                process_binary_data(data, len);
            }
        }
    }
}
