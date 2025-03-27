#include "synchronizer.h"

#include <rom/ets_sys.h>

// 200 мс в микросекундах
#define TIMEOUT_US 200000
// Полная длина синхронизирующей последовательности
#define PATTERN_LENGTH 16
// Минимальное число бит для проверки совпадения (например, если первые биты утеряны)
#define MIN_SYNC_BITS 10

// Синхронизирующая последовательность: 1,0,1,0,1,0,1,0,0,1,0,1,0,1,0,0
const int pattern[PATTERN_LENGTH] = {1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0};

// Функция ожидающая паттерн стартовой последовательности перед каждым сообщением
// При обнаружении таковой в течение TIMEOUT_US мкс сразу же возвращает 1
// При не обнаружении - 0
int await_end_sync(const int period_us, const int analogue_threshold) {
    // Буфер для накопления распознанных битов синхронизации
    int sync_buffer[PATTERN_LENGTH];
    // Текущая длина буфера
    int sync_buffer_len = 0;

    // Прошедшее время (в микросекундах)
    int elapsed = 0;
    // Считываем начальное значение сигнала
    int last_bit = receive_bit(analogue_threshold);
    // Время, сколько микросекунд сигнал оставался неизменным
    int stable_duration = 0;

    // Непрерывное считывание в течение таймаута
    while (elapsed < TIMEOUT_US) {
        ets_delay_us(1);
        elapsed++;
        const int current_bit = receive_bit(analogue_threshold);

        if (current_bit == last_bit) {
            // Сигнал не изменился – увеличиваем длительность текущего уровня
            stable_duration++;
        } else {
            // При обнаружении изменения определяем, сколько битов передавалось:
            // если длительность меньше порога (1.5 * period), считаем, что передан один бит,
            // иначе – два одинаковых бита (для случая, когда 0 горит в 2 раза дольше).
            const int threshold_duration = (int)(1.5 * period_us);
            const int count = (stable_duration < threshold_duration) ? 1 : 2;

            // Добавляем полученный бит count раз в буфер
            for (int j = 0; j < count; j++) {
                if (sync_buffer_len < PATTERN_LENGTH) {
                    sync_buffer[sync_buffer_len++] = last_bit;
                } else {
                    // Если буфер заполнен, сдвигаем его влево и вставляем новый бит в конец
                    for (int i = 0; i < PATTERN_LENGTH - 1; i++) {
                        sync_buffer[i] = sync_buffer[i + 1];
                    }
                    sync_buffer[PATTERN_LENGTH - 1] = last_bit;
                }
            }

            // Если накоплено достаточно бит (например, MIN_SYNC_BITS),
            // проверяем, соответствует ли текущий буфер соответствующему суффиксу синхронизирующей последовательности.
            if (sync_buffer_len >= MIN_SYNC_BITS) {
                // Если буфер не заполнен полностью, сравниваем с суффиксом полной последовательности
                const int start_index = PATTERN_LENGTH - sync_buffer_len;
                int match = 1;
                for (int i = 0; i < sync_buffer_len; i++) {
                    if (sync_buffer[i] != pattern[start_index + i]) {
                        match = 0;
                        break;
                    }
                }
                if (match) {
                    // Если совпадает, считаем, что синхронизация завершена
                    return 1;
                }
            }

            // Обновляем значения для следующей итерации
            last_bit = current_bit;
            stable_duration = 0;
        }
    }
    // Если время ожидания истекло, а последовательность не обнаружена – возвращаем 0
    return 0;
}
