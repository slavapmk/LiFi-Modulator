#ifndef SYNCHRONIZER_H
#define SYNCHRONIZER_H

// Объявления внешних функций и переменной:
// Функция получения бита (возвращает 0 или 1)
extern int receive_bit(int threshold);

int await_end_sync(int period_us);

#endif //SYNCHRONIZER_H
