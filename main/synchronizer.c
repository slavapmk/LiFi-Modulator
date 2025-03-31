#include "synchronizer.h"

#include <esp_timer.h>
#include <rtc_wdt.h>
#include <time.h>
#include <driver/adc.h>
#include <driver/uart.h>
#include <hal/wdt_hal.h>
#include <rom/ets_sys.h>

// 200 мс в микросекундах
#define TIMEOUT_US 200000
// Размер битового буфера для синхронизации
#define SYNC_BUFFER_LENGTH 16
// Полная длина синхронизирующей последовательности
#define PATTERN_LENGTH 16
// Минимальное число бит для проверки совпадения (например, если первые биты утеряны)
#define MIN_SYNC_BITS 10
// Размер буфера сканирования для усреднения
#define READ_BUFFER_LENGTH 10
// Максимальный период для одного бита в микросекундах
#define MAX_STABLE_DURATION 30000
#define SYNC_DELAY          40000

// Синхронизирующая последовательность: 1,0,1,0,1,0,1,0,0,1,0,1,0,1,0,0
const int pattern[PATTERN_LENGTH] = {1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1};

int read_avg(const int length_us, const int analogue_threshold) {
    long long int sum = 0;
    int count = 0;
    for (int i = 0; i < length_us; ++i) {
        const int read = adc1_get_raw(ADC1_CHANNEL_4);
        sum += read;
        ++count;
        ets_delay_us(1);
    }
    return (sum / count) > analogue_threshold;
}

void shift_left_and_append_int(int arr[], const int size, const int new_value) {
    for (int i = 0; i < size - 1; ++i) {
        arr[i] = arr[i + 1]; // Сдвигаем влево
    }
    arr[size - 1] = new_value; // Добавляем новое значение в конец
}

void shift_left_and_append_double(double arr[], const int size, const double new_value) {
    for (int i = 0; i < size - 1; ++i) {
        arr[i] = arr[i + 1]; // Сдвигаем влево
    }
    arr[size - 1] = new_value; // Добавляем новое значение в конец
}

// Функция определения бита по среднему значению измерений (для стабильности на низкой частоте синхронизации)
double avg_bin_of_buffer(const int arr[], const int size, const int analogue_threshold) {
    long long int sum = 0;
    int count = 0;
    for (int i = 0; i < size; ++i) {
        if (arr[i] != -1) {
            sum += arr[i];
            ++count;
        }
    }
    return sum * 1.0 / count / analogue_threshold;
}

static char console_buffer[1024]; // Глобальный буфер (можно изменить размер по необходимости)

void print_int_array(double arr[], const int size) {
    int offset = 0;
    for (int i = 0; i < size; i++) {
        offset += snprintf(console_buffer + offset, sizeof(console_buffer) - offset, "%.02f ", arr[i]);
    }
    console_buffer[offset] = '\r'; // Перевод строки с помощью \r\n
    console_buffer[offset + 1] = '\n';
    console_buffer[offset + 2] = '\0'; // Завершающий нулевой символ

    uart_write_bytes(UART_NUM_0, console_buffer, strlen(console_buffer));
}

void print_double_array(int arr[], const int size) {
    int offset = 0;
    for (int i = 0; i < size; i++) {
        offset += snprintf(console_buffer + offset, sizeof(console_buffer) - offset, "%4d ", arr[i]);
    }
    console_buffer[offset] = '\r'; // Перевод строки с помощью \r\n
    console_buffer[offset + 1] = '\n';
    console_buffer[offset + 2] = '\0'; // Завершающий нулевой символ

    uart_write_bytes(UART_NUM_0, console_buffer, strlen(console_buffer));
}

static double sync_buffer[SYNC_BUFFER_LENGTH];
static int read_buffer[READ_BUFFER_LENGTH];

void clear_read_buffer(void) {
    for (int i = 0; i < READ_BUFFER_LENGTH; ++i) {
        read_buffer[i] = -1;
    }
}

int check_buffers() {
    for (int i = SYNC_BUFFER_LENGTH - 1; i >= 0; --i) {
        if ((pattern[i] >= 1) != (sync_buffer[i] >= 1)) {
            // printf("Incorrect\r\n");
            return false;
        }
        if (sync_buffer[i] == -1 && (SYNC_BUFFER_LENGTH - i) > MIN_SYNC_BITS) {
            // printf("Correct suffix\r\n");
            return true;
        }
    }

    // printf("All correct\r\n");
    return true;
}

void init_synchronizer() {
    for (int i = 0; i < SYNC_BUFFER_LENGTH; ++i) {
        sync_buffer[i] = 0;
    }
}

// Функция ожидающая паттерн стартовой последовательности перед каждым сообщением
// При обнаружении таковой в течение TIMEOUT_US мкс сразу же возвращает 1
// При не обнаружении - 0
int await_end_sync(const int analogue_threshold) {
    clear_read_buffer();

    // int time = 0;

    double last_bit = -1;
    const int64_t start_time = esp_timer_get_time();
    int64_t stable_duration_start = 0;
    int filled_count = 0;
    while (1) {
        rtc_wdt_feed();
        ets_delay_us(1);

        shift_left_and_append_int(
            read_buffer, READ_BUFFER_LENGTH, adc1_get_raw(ADC1_CHANNEL_4)
        );
        ++filled_count;
        const double bin_of_buffer = avg_bin_of_buffer(read_buffer, READ_BUFFER_LENGTH, analogue_threshold);
        if ((bin_of_buffer >= 1) != (last_bit >= 1) && filled_count > 5) {
            if (last_bit != -1) {
                const int64_t diff = esp_timer_get_time() - stable_duration_start;

                for (int i = 0; i < (diff > MAX_STABLE_DURATION ? 2 : 1); ++i) {
                    // for (int i = 0; i < (diff / max_stable_duration); ++i) {
                    shift_left_and_append_double(sync_buffer, SYNC_BUFFER_LENGTH, last_bit);
                }

                // char console_buffer[32];
                // snprintf(console_buffer, sizeof(console_buffer), "Delay %6lld / %6d \r\n", diff, MAX_STABLE_DURATION);
                // uart_write_bytes(UART_NUM_0, console_buffer, strlen(console_buffer));
            }
            // print_int_array(sync_buffer, SYNC_BUFFER_LENGTH);
            // print_double_array(read_buffer, READ_BUFFER_LENGTH);
            stable_duration_start = esp_timer_get_time();
            // const char* aaa = "\r\n\0";
            // uart_write_bytes(UART_NUM_0, aaa, strlen(aaa));
            last_bit = bin_of_buffer;
            clear_read_buffer();
            filled_count = 0;
            if (check_buffers()) {
                for (int i = 0; i < SYNC_BUFFER_LENGTH; ++i) {
                    sync_buffer[i] = 0;
                }
                for (int i = 0; i < SYNC_DELAY / 1000; ++i) {
                    ets_delay_us(1000);
                }
                return 1;
            }
        }

        if (esp_timer_get_time() - start_time > 2000000) {
            // if (esp_timer_get_time() - start_time > 200000000) {
            const char* console_buffer = "Sync timeout\r\n\0";
            uart_write_bytes(UART_NUM_0, console_buffer, strlen(console_buffer));
            return 0;
        }
    }
}
