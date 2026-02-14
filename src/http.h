#ifndef HTTP_H
#define HTTP_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char method[8];
    char path[256];
    char content_type[128];
    size_t content_length;
    unsigned char *body;
    size_t body_length;
} HttpRequest;

bool read_http_request(int client_fd, HttpRequest *request, int *status_code);
void free_http_request(HttpRequest *request);

void send_http_response(int client_fd,
                        const char *status,
                        const char *content_type,
                        const void *body,
                        size_t body_length,
                        const char *extra_headers);

void send_error_response(int client_fd, int status_code);

#endif
