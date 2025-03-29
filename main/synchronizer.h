#ifndef SYNCHRONIZER_H
#define SYNCHRONIZER_H

// Внешня функция получения бита (возвращает 0 или 1)
extern int receive_bit(int threshold);

int await_end_sync(int period_us, int analogue_threshold);

#endif //SYNCHRONIZER_H
