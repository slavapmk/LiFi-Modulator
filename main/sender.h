#ifndef SENDER_H
#define SENDER_H

#include <stdint.h>  // Add this line to define uint8_t

void send_manchester_bit(const int bit, const double baseFrequency);
void process_binary_data(const uint8_t* data, const int len, const double baseFrequency);

#endif
