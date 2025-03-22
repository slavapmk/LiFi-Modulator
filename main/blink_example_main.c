#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_system.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <rom/gpio.h>

#define LED_GPIO         2         // Пин светодиода
#define UART_PORT_NUM    UART_NUM_0  // UART, используемый для связи (USB)
#define BUF_SIZE         1024      // Размер буфера для приёма данных

// Глобальная переменная для частоты (в Гц)
volatile float frequency = 2.0;

// Функция для обработки специальных команд
void process_command(const char *cmd) {
    // Если команда начинается с "#FREQ"
    if (strncmp(cmd, "#FREQ", 5) == 0) {
        // Пропускаем "#FREQ" и пробельные символы
        const char *arg = cmd + 5;
        while (*arg == ' ' || *arg == '\t') {
            arg++;
        }
        if (*arg) {
            float new_freq = atof(arg);
            if (new_freq > 0) {
                frequency = new_freq;
                printf("Частота изменена на %.2f Гц\n", frequency);
                // Здесь можно отправить команду на устройство или выполнить дополнительную обработку
            } else {
                printf("Неверное значение частоты: %s\n", arg);
            }
        } else {
            printf("Команда #FREQ требует аргумент, например: #FREQ 2\n");
        }
    } else {
        printf("Неизвестная команда: %s\n", cmd);
    }
}

void app_main(void)
{
    // Инициализация пина для светодиода
    gpio_pad_select_gpio(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);

    // Настройка UART
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);

    uint8_t data[BUF_SIZE];

    while (1) {
        // Чтение данных с UART (блокирующее чтение с таймаутом)
        int len = uart_read_bytes(UART_PORT_NUM, data, BUF_SIZE - 1, 20 / portTICK_PERIOD_MS);
        if (len > 0) {
            data[len] = '\0';  // завершаем строку нулевым символом

            // Удаляем символы перевода строки и возврата каретки
            char *pos;
            if ((pos = strchr((char *)data, '\r')) != NULL) {
                *pos = '\0';
            }
            if ((pos = strchr((char *)data, '\n')) != NULL) {
                *pos = '\0';
            }

            // Если строка начинается с '#' — обрабатываем её как команду
            if (data[0] == '#') {
                process_command((char *)data);
            }
            else {
                // Если строка содержит только '0' и '1', обрабатываем её как последовательность для мигания
                int i = 0;
                while (data[i] != '\0') {
                    if (data[i] != '0' && data[i] != '1') {
                        // Пропускаем некорректные символы
                        i++;
                        continue;
                    }
                    char current = data[i];
                    int count = 1;
                    // Группируем подряд идущие одинаковые символы
                    while (data[i + 1] != '\0' && data[i + 1] == current) {
                        count++;
                        i++;
                    }
                    // Устанавливаем состояние светодиода
                    if (current == '1') {
                        gpio_set_level(LED_GPIO, 1);
                    } else {
                        gpio_set_level(LED_GPIO, 0);
                    }
                    // Рассчитываем длительность задержки в миллисекундах:
                    // период = 1000 / frequency (мс)
                    int delay_ms = (int)(1000.0 / frequency);
                    for (int j = 0; j < count; j++) {
                        vTaskDelay(delay_ms / portTICK_PERIOD_MS);
                    }
                    i++;
                }
                // По окончании последовательности выключаем светодиод
                gpio_set_level(LED_GPIO, 0);
            }
        }
    }
}
