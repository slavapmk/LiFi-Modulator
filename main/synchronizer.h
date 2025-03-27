#ifndef SYNCHRONIZER_H
#define SYNCHRONIZER_H

// Объявления внешних функций и переменной:
// Функция получения бита (возвращает 0 или 1)
extern int recieve_bit(void);
// Функция задержки в микросекундах
extern void delay_us(int microseconds);
// Глобальная переменная, задающая длительность одного бита (в микросекундах)
extern int period;

int await_end_sync(void);

#endif //SYNCHRONIZER_H
