#include "utils.h"

#include <string.h>

void shift_left_and_append_int(int arr[], const int size, const int new_value) {
    if (!arr) return;
    if (size <= 0) return;
    memmove(arr, arr + 1, (size - 1) * sizeof(int));
    arr[size - 1] = new_value;
}

void shift_left_and_append_double(double arr[], const int size, const double new_value) {
    if (!arr) return;
    if (size <= 0) return;
    memmove(arr, arr + 1, (size - 1) * sizeof(int));
    arr[size - 1] = new_value;
}

void shift_left_and_append_char(char arr[], const int size, const char new_value) {
    if (!arr) return;
    if (size <= 0) return;
    memmove(arr, arr + 1, (size - 1) * sizeof(int));
    arr[size - 1] = new_value;
}

double avg_bin_of_buffer(const int arr[], const int size, const int analogue_threshold) {
    long long int sum = 0;
    int count = 0;
    for (int i = 0; i < size; ++i) {
        if (arr[i] != -1) {
            sum += arr[i];
            ++count;
        }
    }
    if (count == 0) {
        return -1;
    }
    return sum * 1.0 / count / analogue_threshold;
}
