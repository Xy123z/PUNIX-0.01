#ifndef SERIAL_H
#define SERIAL_H

void serial_init(void);
void serial_putchar_port(int index, char a);
void serial_print(const char* str);

#endif
