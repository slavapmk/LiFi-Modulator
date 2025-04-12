#include "utils.h"

#include <string.h>

// void shift_left_and_append_int(int arr[], const int size, const int new_value) {
//     if (!arr) return;
//     if (size <= 0) return;
//     memmove(arr, arr + 1, (size - 1) * sizeof(int));
//     arr[size - 1] = new_value;
// }
//
// void shift_left_and_append_double(double arr[], const int size, const double new_value) {
//     if (!arr) return;
//     if (size <= 0) return;
//     memmove(arr, arr + 1, (size - 1) * sizeof(int));
//     arr[size - 1] = new_value;
// }
//
// void shift_left_and_append_char(char arr[], const int size, const char new_value) {
//     if (!arr) return;
//     if (size <= 0) return;
//     memmove(arr, arr + 1, (size - 1) * sizeof(int));
//     arr[size - 1] = new_value;
// }

// double avg_bin_of_buffer(const int arr[], const int size, const int analogue_threshold) {
//     long long int sum = 0;
//     int count = 0;
//     for (int i = 0; i < size; ++i) {
//         if (arr[i] != -1) {
//             sum += arr[i];
//             ++count;
//         }
//     }
//     if (count == 0) {
//         return -1;
//     }
//     return sum * 1.0 / count / analogue_threshold;
// }
#include <stdlib.h>
#include <stdio.h>

// Обмен элементов
static void swap(int* a, int* b) {
    int t = *a;
    *a = *b;
    *b = t;
}

// Выбор медианы трех для улучшения выбора опорного элемента
static int median_of_three(int arr[], int left, int right) {
    int mid = left + (right - left) / 2;
    if (arr[right] < arr[left]) swap(&arr[left], &arr[right]);
    if (arr[mid] < arr[left]) swap(&arr[mid], &arr[left]);
    if (arr[right] < arr[mid]) swap(&arr[right], &arr[mid]);
    return mid;
}

// Разбиение Хоара
static int partition(int arr[], int left, int right) {
    int pivot_index = median_of_three(arr, left, right);
    int pivot = arr[pivot_index];
    swap(&arr[pivot_index], &arr[right]);

    int store_index = left;
    for (int i = left; i < right; ++i) {
        if (arr[i] < pivot) {
            swap(&arr[store_index], &arr[i]);
            store_index++;
        }
    }

    swap(&arr[right], &arr[store_index]);
    return store_index;
}

// Итеративный Quickselect
static int quickselect(int arr[], int size, int k) {
    int left = 0, right = size - 1;

    while (left <= right) {
        if (left == right) return arr[left];

        int pivot_index = partition(arr, left, right);

        if (k == pivot_index) {
            return arr[k];
        } else if (k < pivot_index) {
            right = pivot_index - 1;
        } else {
            left = pivot_index + 1;
        }
    }

    return -1; // Ошибка
}

// Расчет медианы
int calc_median(const int arr[], int size) {
    if (size == 0 || arr == NULL) return -1; // Некорректные данные

    // Создаем динамические копии
    int* copy1 = (int*)malloc(size * sizeof(int));
    int* copy2 = (int*)malloc(size * sizeof(int));

    if (!copy1 || !copy2) {
        free(copy1);
        free(copy2);
        return -1; // Ошибка выделения памяти
    }

    memcpy(copy1, arr, size * sizeof(int));
    int result;

    if (size % 2 == 1) {
        result = quickselect(copy1, size, size / 2);
    } else {
        memcpy(copy2, arr, size * sizeof(int));
        int m1 = quickselect(copy1, size, size / 2 - 1);
        int m2 = quickselect(copy2, size, size / 2);
        result = (m1 + m2) / 2;
        free(copy2);
    }

    free(copy1);
    return result;
}