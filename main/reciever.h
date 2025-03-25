//
// Created by Slava on 25.03.2025.
//

#ifndef RECIEVER_H
#define RECIEVER_H
#include <hal/uart_types.h>

void process_manchester_receive(
    int threshold, double baseFrequency,
    uart_port_t uart_port
);
void test_recieve_all(int threshold);

#endif
