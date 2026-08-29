#include "serial.h"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
static speed_t baud_rate(int baud) {
    switch (baud) { case 1200:return B1200; case 2400:return B2400; case 4800:return B4800; case 19200:return B19200; case 38400:return B38400; case 57600:return B57600; case 115200:return B115200; default:return B9600; }
}
int im_serial_open(const char *path, int baud) {
    int fd = open(path ? path : "", O_RDWR | O_NOCTTY | O_NONBLOCK); if (fd < 0) return -1;
    struct termios tio; if (tcgetattr(fd, &tio) != 0) { close(fd); return -1; }
    cfmakeraw(&tio); speed_t rate = baud_rate(baud); cfsetispeed(&tio, rate); cfsetospeed(&tio, rate);
    tio.c_cflag |= CLOCAL | CREAD; tio.c_cc[VMIN] = 0; tio.c_cc[VTIME] = 1;
    if (tcsetattr(fd, TCSANOW, &tio) != 0) { close(fd); return -1; } return fd;
}
int im_serial_write(int handle, const char *data, size_t length) { if (handle < 0 || !data) return -1; ssize_t n = write(handle, data, length); return n < 0 ? -1 : (int)n; }
int im_serial_read(int handle, char *buffer, size_t capacity) { if (handle < 0 || !buffer || !capacity) return -1; ssize_t n = read(handle, buffer, capacity); return n < 0 ? 0 : (int)n; }
int im_serial_close(int handle) { return handle >= 0 && close(handle) == 0 ? 0 : -1; }
