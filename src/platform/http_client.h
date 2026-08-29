#ifndef INIMERSE_HTTP_CLIENT_H
#define INIMERSE_HTTP_CLIENT_H
#include <stddef.h>

/* Dependency-free HTTP/1.1 client over the socket PAL. HTTPS is intentionally
 * rejected until a TLS PAL is available. Returns 0 on transport success. */
int im_http_request(const char *method, const char *url, const char *body,
                    char *response, size_t capacity, int *status);
#endif
