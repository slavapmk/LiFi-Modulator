#ifndef SENDER_H
#define SENDER_H

#include <stdint.h>

void send_manchester_bit(int bit, int half_period_us);
void process_binary_data(const uint8_t* data, int len, double baseFrequency);

#endif
