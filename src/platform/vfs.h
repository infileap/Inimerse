#ifndef INIMERSE_VFS_H
#define INIMERSE_VFS_H

#include <stddef.h>

/* V0.4 virtual filesystem. Paths are resolved through a named mount prefix
   (os:/, mem:/, pkg:/ and cache:/ are reserved). */
typedef struct ImVfs ImVfs;

ImVfs *im_vfs_create(void);
void im_vfs_destroy(ImVfs *vfs);
int im_vfs_mount_os(ImVfs *vfs, const char *prefix, const char *root);
int im_vfs_read_file(ImVfs *vfs, const char *path, char **out_data, size_t *out_len);
int im_vfs_write_file(ImVfs *vfs, const char *path, const void *data, size_t len);
int im_vfs_normalize(const char *path, char *out, size_t out_cap);

#endif
