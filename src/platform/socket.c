#include "socket.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct ImSocket {
#ifdef _WIN32
    uintptr_t fd;
#else
    int fd;
#endif
};

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
static int socket_valid(uintptr_t fd) { return fd != INVALID_SOCKET; }
int im_socket_init(void) { WSADATA data; return WSAStartup(MAKEWORD(2,2), &data) == 0 ? 0 : -1; }
void im_socket_shutdown(void) { WSACleanup(); }
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
static int socket_valid(int fd) { return fd >= 0; }
int im_socket_init(void) { return 0; }
void im_socket_shutdown(void) {}
#endif

/* Included after the platform headers on purpose: thread.h pulls in <windows.h>,
 * and <windows.h> before <winsock2.h> makes winsock2.h emit
 * "#warning Please include winsock2.h before windows.h" -- a new warning this
 * file did not have before the bounded resolve moved onto a helper thread. */
#include "thread.h"

static uint64_t socket_now_ms(void) {
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000u);
#endif
}

static ImSocket *wrap_socket(
#ifdef _WIN32
    uintptr_t fd
#else
    int fd
#endif
) {
    if (!socket_valid(fd)) return NULL;
    ImSocket *socket = (ImSocket *)calloc(1, sizeof(*socket));
    if (!socket) {
#ifdef _WIN32
        closesocket((SOCKET)fd);
#else
        close(fd);
#endif
        return NULL;
    }
    socket->fd = fd; return socket;
}

#ifdef _WIN32
/* InetPtonA carries inet_pton's contract: 1 on success, 0 when the string is not
 * a valid literal for that family, -1 on error. */
#define IM_INET_PTON(family, src, dst) InetPtonA((family), (src), (dst))
#else
#define IM_INET_PTON(family, src, dst) inet_pton((family), (src), (dst))
#endif

/* A host that is already an address literal does not need a resolver.
 *
 * getaddrinfo() does accept "127.0.0.1" and "::1" and hands them back unchanged,
 * but it reaches them through the full resolver path, and that path is the one
 * that stalls when a machine's resolver is slow or unreachable.  Measured on
 * Windows (ucrt64) by timing each socket call in one probe round: im_socket_init
 * stayed at 1-4 ms while im_socket_listen took 0.9-3.7 s, port_available up to
 * 1478 ms, connect 1542 ms, port_open 2116 ms -- four resolutions per round, so
 * the round's wall time ranged from 151 ms to 8564 ms and the CTest 10 s ceiling
 * turned the tail red.  inet_pton() answers a literal with no I/O and no
 * resolver thread.
 *
 * This is a fast path, not the answer: anything inet_pton() rejects falls through
 * to getaddrinfo() exactly as before, so a hostname, an empty host, and the
 * AI_PASSIVE (no host) case keep their previous behaviour -- including the
 * bounded-resolution machinery below, which still covers every non-literal.
 * Both resolve entry points consult this one helper so the fast path cannot
 * become a second rule.  See docs/AUDIT.md §1.63. */
static int addr_from_literal(const char *host, uint16_t port, struct sockaddr_storage *out, socklen_t *out_len) {
    if (!host || !*host) return -1;
    struct sockaddr_in v4; struct sockaddr_in6 v6;
    memset(&v4, 0, sizeof v4); memset(&v6, 0, sizeof v6);
    if (IM_INET_PTON(AF_INET, host, &v4.sin_addr) == 1) {
        v4.sin_family = AF_INET; v4.sin_port = htons(port);
        memcpy(out, &v4, sizeof v4); *out_len = (socklen_t)sizeof v4; return 0;
    }
    if (IM_INET_PTON(AF_INET6, host, &v6.sin6_addr) == 1) {
        v6.sin6_family = AF_INET6; v6.sin6_port = htons(port);
        memcpy(out, &v6, sizeof v6); *out_len = (socklen_t)sizeof v6; return 0;
    }
    return -1;
}

static int resolve_addr(const char *host, uint16_t port, struct sockaddr_storage *out, socklen_t *out_len, int passive) {
    if (addr_from_literal(host, port, out, out_len) == 0) return 0;
    struct addrinfo hints = {0}, *result = NULL;
    char service[16]; snprintf(service, sizeof service, "%u", (unsigned)port);
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_flags = passive ? AI_PASSIVE : 0;
    if (getaddrinfo((host && *host) ? host : NULL, service, &hints, &result) != 0 || !result) return -1;
    if (result->ai_addrlen > sizeof(*out)) { freeaddrinfo(result); return -1; }
    memcpy(out, result->ai_addr, result->ai_addrlen); *out_len = (socklen_t)result->ai_addrlen;
    freeaddrinfo(result); return 0;
}

/* Name resolution runs on a helper thread, because getaddrinfo() has no timeout
 * in any standard and it is the step that actually stalls when a host's resolver
 * is unreachable: a blocked resolver waits out the system's own retry schedule,
 * which is minutes, not milliseconds.
 *
 * im_socket_connect_timeout() promises a bound, and a promise that stops short of
 * the slowest step is not a promise.  Before this, http_get() against a host
 * whose DNS was black-holed hung the engine until the OS resolver gave up -- on
 * a runner with no DNS egress that is the whole test timeout, and it looks like
 * "21x slower" rather than "stopped".  The TCP connect below was always bounded;
 * only the name lookup was not.
 *
 * getaddrinfo() cannot be cancelled, so on expiry the thread is detached and its
 * job is deliberately leaked: freeing it would be a use-after-free against a
 * thread still writing it.  One leak per stalled resolution is the honest price
 * of a bound that holds.  Where the platform has no timed join (POSIX outside
 * Linux) this degrades to the untimed resolve it replaces, which is no worse. */
typedef struct {
    char host[256];
    char service[16];
    struct addrinfo hints;
    struct addrinfo *result;
    int rc;
} ResolveJob;

#ifdef _WIN32
static unsigned __stdcall resolve_job_run(void *raw) {
#else
static void *resolve_job_run(void *raw) {
#endif
    ResolveJob *job = (ResolveJob *)raw;
    job->rc = getaddrinfo(job->host[0] ? job->host : NULL, job->service, &job->hints, &job->result);
    if (job->rc != 0) job->result = NULL;
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static int resolve_addr_timed(const char *host, uint16_t port, struct sockaddr_storage *out,
                              socklen_t *out_len, int passive, int timeout_ms) {
    if (timeout_ms <= 0) return resolve_addr(host, port, out, out_len, passive);
    /* The literal fast path has to sit here too, not only inside resolve_addr():
     * a bound that is only honoured after a resolver round trip is not a bound,
     * and spawning a thread to resolve "127.0.0.1" is what made the Windows probe
     * slow.  Both entries ask the same helper, so there is one rule. */
    if (addr_from_literal(host, port, out, out_len) == 0) return 0;
    ResolveJob *job = (ResolveJob *)calloc(1, sizeof(*job));
    if (!job) return resolve_addr(host, port, out, out_len, passive);
    if (host && *host) snprintf(job->host, sizeof job->host, "%s", host);
    snprintf(job->service, sizeof job->service, "%u", (unsigned)port);
    job->hints.ai_family = AF_UNSPEC; job->hints.ai_socktype = SOCK_STREAM;
    job->hints.ai_flags = passive ? AI_PASSIVE : 0;

    void *thread = im_thread_start(resolve_job_run, job);
    if (!thread) { free(job); return resolve_addr(host, port, out, out_len, passive); }
    if (im_thread_join(thread, (unsigned int)timeout_ms) != 0) {
        im_thread_detach(thread);
        return -1;   /* job intentionally leaked; the resolver is still using it */
    }
    int rc = job->rc;
    struct addrinfo *result = job->result;
    free(job);
    if (rc != 0 || !result) { if (result) freeaddrinfo(result); return -1; }
    if (result->ai_addrlen > sizeof(*out)) { freeaddrinfo(result); return -1; }
    memcpy(out, result->ai_addr, result->ai_addrlen); *out_len = (socklen_t)result->ai_addrlen;
    freeaddrinfo(result); return 0;
}

ImSocket *im_socket_listen(const char *host, uint16_t port, int backlog) {
    struct sockaddr_storage addr; socklen_t addr_len;
    if (resolve_addr(host, port, &addr, &addr_len, 1) != 0) return NULL;
#ifdef _WIN32
    SOCKET fd = socket(addr.ss_family, SOCK_STREAM, IPPROTO_TCP); if (fd == INVALID_SOCKET) return NULL;
    BOOL yes = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
#else
    int fd = socket(addr.ss_family, SOCK_STREAM, 0); if (fd < 0) return NULL;
    int yes = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
#endif
    if (bind(fd, (struct sockaddr *)&addr, addr_len) != 0 || listen(fd, backlog > 0 ? backlog : 16) != 0) {
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
        return NULL;
    }
    return wrap_socket(fd);
}

ImSocket *im_socket_connect_timeout(const char *host, uint16_t port, int timeout_ms) {
    struct sockaddr_storage addr; socklen_t addr_len;
    if (timeout_ms <= 0) timeout_ms = 2000;
    if (resolve_addr_timed(host, port, &addr, &addr_len, 0, timeout_ms) != 0) return NULL;
#ifdef _WIN32
    SOCKET fd = socket(addr.ss_family, SOCK_STREAM, IPPROTO_TCP); if (fd == INVALID_SOCKET) return NULL;
    u_long mode = 1;
    if (ioctlsocket(fd, FIONBIO, &mode) != 0) { closesocket(fd); return NULL; }
#else
    int fd = socket(addr.ss_family, SOCK_STREAM, 0); if (fd < 0) return NULL;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) { close(fd); return NULL; }
#endif
    int rc = connect(fd, (struct sockaddr *)&addr, addr_len);
    int connected = (rc == 0);
    if (!connected) {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) { closesocket(fd); return NULL; }
#else
        if (errno != EINPROGRESS) { close(fd); return NULL; }
#endif
        uint64_t deadline = socket_now_ms() + (uint64_t)timeout_ms;
        int ready = 0;
        do {
            uint64_t now = socket_now_ms();
            if (now >= deadline) { rc = 0; break; }
            uint64_t remain = deadline - now;
            fd_set wfds; FD_ZERO(&wfds); FD_SET(fd, &wfds);
            struct timeval tv = { (long)(remain / 1000u), (long)(remain % 1000u) * 1000L };
            rc = select((int)fd + 1, NULL, &wfds, NULL, &tv);
            ready = rc > 0 && FD_ISSET(fd, &wfds);
#ifndef _WIN32
        } while (rc < 0 && errno == EINTR);
#else
        } while (0);
#endif
        if (rc > 0 && ready) {
            int so_error = 0; socklen_t so_len = sizeof(so_error);
#ifdef _WIN32
            getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&so_error, &so_len);
#else
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_len);
#endif
            connected = (so_error == 0);
        }
    }
    if (!connected) {
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
        return NULL;
    }
#ifdef _WIN32
    mode = 0; ioctlsocket(fd, FIONBIO, &mode);
#else
    fcntl(fd, F_SETFL, flags);
#endif
    return wrap_socket(fd);
}

ImSocket *im_socket_connect(const char *host, uint16_t port) {
    return im_socket_connect_timeout(host, port, 2000);
}

ImSocket *im_socket_accept(ImSocket *listener) {
    if (!listener) return NULL;
#ifdef _WIN32
    SOCKET fd = accept((SOCKET)listener->fd, NULL, NULL);
#else
    int fd;
    do { fd = accept(listener->fd, NULL, NULL); } while (fd < 0 && errno == EINTR);
#endif
    return wrap_socket(fd);
}
int im_socket_send(ImSocket *socket, const void *data, size_t length) {
    if (!socket || !data) return -1;
#ifdef _WIN32
    return send((SOCKET)socket->fd, (const char *)data, (int)length, 0);
#else
    int n;
    int send_flags = 0;
#ifdef MSG_NOSIGNAL
    send_flags |= MSG_NOSIGNAL;
#endif
    do { n = (int)send(socket->fd, data, length, send_flags); } while (n < 0 && errno == EINTR);
    return n;
#endif
}
int im_socket_recv(ImSocket *socket, void *buffer, size_t capacity) {
    if (!socket || !buffer || !capacity) return -1;
#ifdef _WIN32
    return recv((SOCKET)socket->fd, (char *)buffer, (int)capacity, 0);
#else
    int n; do { n = (int)recv(socket->fd, buffer, capacity, 0); } while (n < 0 && errno == EINTR);
    return n;
#endif
}
int im_socket_peek(ImSocket *socket) {
    if (!socket) return -1;
    char byte;
#ifdef _WIN32
    return recv((SOCKET)socket->fd, &byte, 1, MSG_PEEK);
#else
    int n; do { n = (int)recv(socket->fd, &byte, 1, MSG_PEEK); } while (n < 0 && errno == EINTR);
    return n;
#endif
}
int im_socket_set_nonblocking(ImSocket *socket, int enabled) {
    if (!socket) return -1;
#ifdef _WIN32
    u_long mode = enabled ? 1 : 0; return ioctlsocket((SOCKET)socket->fd, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(socket->fd, F_GETFL, 0); if (flags < 0) return -1; return fcntl(socket->fd, F_SETFL, enabled ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
#endif
}
int im_socket_last_error(void) {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}
int im_socket_would_block(void) {
#ifdef _WIN32
    int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}
int im_socket_local_port(ImSocket *socket) {
    if (!socket) return -1;
    struct sockaddr_storage addr; socklen_t len = sizeof(addr);
#ifdef _WIN32
    if (getsockname((SOCKET)socket->fd, (struct sockaddr *)&addr, &len) != 0) return -1;
#else
    if (getsockname(socket->fd, (struct sockaddr *)&addr, &len) != 0) return -1;
#endif
    if (addr.ss_family == AF_INET) return (int)ntohs(((struct sockaddr_in *)&addr)->sin_port);
    if (addr.ss_family == AF_INET6) return (int)ntohs(((struct sockaddr_in6 *)&addr)->sin6_port);
    return -1;
}
int im_socket_port_open(const char *host, uint16_t port, int timeout_ms) {
    ImSocket *s = im_socket_connect_timeout(host, port, timeout_ms);
    if (!s) return 0;
    im_socket_close(s);
    return 1;
}
int im_socket_port_available(const char *host, uint16_t port) {
    ImSocket *s = im_socket_listen(host, port, 1);
    if (!s) return 0;
    im_socket_close(s);
    return 1;
}
void im_socket_close(ImSocket *socket) {
    if (!socket) return;
#ifdef _WIN32
    closesocket((SOCKET)socket->fd);
#else
    close(socket->fd);
#endif
    free(socket);
}
