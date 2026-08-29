#if !defined(_WIN32)
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif
#include "platform.h"
#include "sync.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

struct ImMutex {
#ifdef _WIN32
    CRITICAL_SECTION value;
#else
    pthread_mutex_t value;
#endif
};

ImMutex *im_mutex_new(void) {
    ImMutex *mutex = (ImMutex *)malloc(sizeof(*mutex));
    if (!mutex) return NULL;
#ifdef _WIN32
    InitializeCriticalSection(&mutex->value);
#else
    if (pthread_mutex_init(&mutex->value, NULL) != 0) { free(mutex); return NULL; }
#endif
    return mutex;
}

void im_mutex_free(ImMutex *mutex) {
    if (!mutex) return;
#ifdef _WIN32
    DeleteCriticalSection(&mutex->value);
#else
    pthread_mutex_destroy(&mutex->value);
#endif
    free(mutex);
}

void im_mutex_lock(ImMutex *mutex) {
    if (!mutex) return;
#ifdef _WIN32
    EnterCriticalSection(&mutex->value);
#else
    pthread_mutex_lock(&mutex->value);
#endif
}

void im_mutex_unlock(ImMutex *mutex) {
    if (!mutex) return;
#ifdef _WIN32
    LeaveCriticalSection(&mutex->value);
#else
    pthread_mutex_unlock(&mutex->value);
#endif
}

uint64_t im_platform_now_ms(void) {
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000u);
#endif
}

void im_platform_sleep_ms(unsigned int milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    struct timespec req = { milliseconds / 1000u, (long)(milliseconds % 1000u) * 1000000L };
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {}
#endif
}

int im_platform_mkdirs(const char *path) {
    if (!path || !*path) return -1;
    char buf[4096];
    size_t n = strlen(path);
    if (n >= sizeof(buf)) return -1;
    memcpy(buf, path, n + 1);
    for (char *p = buf + 1; *p; ++p) {
        if (*p != '/' && *p != '\\') continue;
        char saved = *p; *p = '\0';
#ifdef _WIN32
        if (*buf && _mkdir(buf) != 0 && errno != EEXIST) { *p = saved; return -1; }
#else
        if (*buf && mkdir(buf, 0755) != 0 && errno != EEXIST) { *p = saved; return -1; }
#endif
        *p = saved;
    }
#ifdef _WIN32
    return (_mkdir(buf) == 0 || errno == EEXIST) ? 0 : -1;
#else
    return (mkdir(buf, 0755) == 0 || errno == EEXIST) ? 0 : -1;
#endif
}

int im_platform_path_join(char *buffer, size_t capacity, const char *base, const char *part) {
    if (!buffer || capacity == 0 || !base || !part) return -1;
    size_t n = strlen(base);
    while (n && (base[n - 1] == '/' || base[n - 1] == '\\')) n--;
    while (*part == '/' || *part == '\\') part++;
#ifdef _WIN32
    int written = snprintf(buffer, capacity, "%.*s\\%s", (int)n, base, part);
#else
    int written = snprintf(buffer, capacity, "%.*s/%s", (int)n, base, part);
#endif
    return (written < 0 || (size_t)written >= capacity) ? -1 : 0;
}

int im_platform_executable_path(char *buffer, size_t capacity) {
    if (!buffer || capacity == 0) return -1;
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, buffer, (DWORD)capacity);
    return (n > 0 && n < capacity) ? (int)n : -1;
#else
#ifdef __APPLE__
    uint32_t size = (uint32_t)capacity;
    if (_NSGetExecutablePath(buffer, &size) != 0 || size >= capacity) return -1;
    buffer[size] = '\0';
    return (int)size;
#else
    ssize_t n = readlink("/proc/self/exe", buffer, capacity - 1);
    if (n < 0 || (size_t)n >= capacity) return -1;
    buffer[n] = '\0';
    return (int)n;
#endif
#endif
}

int im_platform_getenv(const char *name, char *buffer, size_t capacity) {
    if (!name || !*name || !buffer || capacity == 0) return -1;
    const char *value = getenv(name); if (!value) return 1;
    size_t n = strlen(value); if (n >= capacity) return -1;
    memcpy(buffer, value, n + 1); return 0;
}

int im_platform_has_capability(const char *name) {
    if (!name || !*name) return 0;
#ifdef _WIN32
    if (strcmp(name, "native_dll") == 0 || strcmp(name, "gui") == 0 ||
        strcmp(name, "threads") == 0 || strcmp(name, "fiber") == 0 ||
        strcmp(name, "process") == 0 || strcmp(name, "socket") == 0) return 1;
#else
    if (strcmp(name, "posix_fs") == 0 || strcmp(name, "threads") == 0 ||
        strcmp(name, "fiber") == 0 || strcmp(name, "process") == 0 ||
        strcmp(name, "socket") == 0) return 1;
#endif
    return 0;
}

int im_platform_lan_ip(char *buffer, size_t capacity) {
    if (!buffer || capacity < 1) return -1; buffer[0] = 0;
#ifdef _WIN32
    return -1;
#else
    struct ifaddrs *list = NULL; if (getifaddrs(&list) != 0) return -1;
    for (struct ifaddrs *it = list; it; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET || !(it->ifa_flags & IFF_UP) || (it->ifa_flags & IFF_LOOPBACK)) continue;
        if (inet_ntop(AF_INET, &((struct sockaddr_in *)it->ifa_addr)->sin_addr, buffer, capacity)) { freeifaddrs(list); return 0; }
    }
    freeifaddrs(list); return -1;
#endif
}

int im_platform_read_file(const char *path, char *buffer, size_t capacity) {
    if (!path || !buffer || capacity < 1) return -1; FILE *f = fopen(path, "rb"); if (!f) return -1;
    size_t n = fread(buffer, 1, capacity - 1, f); int ok = ferror(f) ? -1 : (int)n; buffer[n] = 0; fclose(f); return ok;
}
int im_platform_write_file(const char *path, const char *data, size_t length) {
    if (!path || (!data && length)) return -1; FILE *f = fopen(path, "wb"); if (!f) return -1;
    size_t n = fwrite(data, 1, length, f); int ok = (n == length && fclose(f) == 0) ? 0 : -1; if (ok) return 0; return -1;
}
