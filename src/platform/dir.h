#ifndef INIMERSE_PLATFORM_DIR_H
#define INIMERSE_PLATFORM_DIR_H

#include <stddef.h>

typedef struct ImDir ImDir;

ImDir *im_dir_open(const char *path);
int im_dir_next(ImDir *dir, char *name, size_t capacity);
int im_dir_next_ex(ImDir *dir, char *name, size_t capacity, int *is_directory);
void im_dir_close(ImDir *dir);

#endif
