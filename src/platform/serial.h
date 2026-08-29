#ifndef INIMERSE_PLATFORM_SERIAL_H
#define INIMERSE_PLATFORM_SERIAL_H
#include <stddef.h>
int im_serial_open(const char *path, int baud);
int im_serial_write(int handle, const char *data, size_t length);
int im_serial_read(int handle, char *buffer, size_t capacity);
int im_serial_close(int handle);
#endif
