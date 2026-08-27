#ifndef INIMERSE_PLATFORM_H
#define INIMERSE_PLATFORM_H

/* Small, dependency-free boundary between the VM and the host operating
 * system.  Keep the core limited to these primitives; GUI/network/installer
 * functionality belongs in modules. */
#include <stddef.h>
#include <stdint.h>
#include "pal_features.h"

#ifdef __cplusplus
extern "C" {
#endif

uint64_t im_platform_now_ms(void);
void im_platform_sleep_ms(unsigned int milliseconds);
int im_platform_mkdirs(const char *path);
int im_platform_executable_path(char *buffer, size_t capacity);
int im_platform_getenv(const char *name, char *buffer, size_t capacity);
int im_platform_has_capability(const char *name);
int im_platform_path_join(char *buffer, size_t capacity, const char *base, const char *part);

#ifdef __cplusplus
}
#endif

#endif
