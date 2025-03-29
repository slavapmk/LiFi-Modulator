//
// Created by Slava on 25.03.2025.
//

#ifndef RECIEVER_H
#define RECIEVER_H
#include <hal/uart_types.h>

// Ожидание и чтение кодированных данных
void process_manchester_receive(
    int threshold, double baseFrequency,
    uart_port_t uart_port
);

// Бинарное чтение строки
void test_receive_all(uart_port_t uart_port, int threshold);

// Аналоговое чтение строки
void test_receive_raw(uart_port_t uart_port);

void init_receiver(void);

#endif
