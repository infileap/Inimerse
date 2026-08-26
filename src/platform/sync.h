#ifndef INIMERSE_PLATFORM_SYNC_H
#define INIMERSE_PLATFORM_SYNC_H

typedef struct ImMutex ImMutex;

ImMutex *im_mutex_new(void);
void im_mutex_free(ImMutex *mutex);
void im_mutex_lock(ImMutex *mutex);
void im_mutex_unlock(ImMutex *mutex);

#endif
