#include "http.h"

#include "server_config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static bool send_all(int client_fd, const void *buffer, size_t length) {
    const unsigned char *data = (const unsigned char *)buffer;
    size_t total = 0;
    while (total < length) {
        ssize_t n = write(client_fd, data + total, length - total);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("write");
            return false;
        }
        total += (size_t)n;
    }
    return true;
}

void send_http_response(int client_fd,
                        const char *status,
                        const char *content_type,
                        const void *body,
                        size_t body_length,
                        const char *extra_headers) {
    char header[1024];
    int n = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "%s"
        "\r\n",
        status,
        content_type,
        body_length,
        extra_headers != NULL ? extra_headers : "");

    if (n < 0 || (size_t)n >= sizeof(header)) {
        return;
    }

    if (!send_all(client_fd, header, (size_t)n)) {
        return;
    }
    if (body != NULL && body_length > 0) {
        (void)send_all(client_fd, body, body_length);
    }
}

void send_error_response(int client_fd, int status_code) {
    switch (status_code) {
    case 400: {
        static const char body[] = "Bad Request";
        send_http_response(client_fd, "400 Bad Request", "text/plain; charset=utf-8", body,
                           sizeof(body) - 1, NULL);
        break;
    }
    case 404: {
        static const char body[] = "Not Found";
        send_http_response(client_fd, "404 Not Found", "text/plain; charset=utf-8", body,
                           sizeof(body) - 1, NULL);
        break;
    }
    case 405: {
        static const char body[] = "Method Not Allowed";
        send_http_response(client_fd, "405 Method Not Allowed", "text/plain; charset=utf-8", body,
                           sizeof(body) - 1, NULL);
        break;
    }
    case 413: {
        static const char body[] = "Payload Too Large";
        send_http_response(client_fd, "413 Payload Too Large", "text/plain; charset=utf-8", body,
                           sizeof(body) - 1, NULL);
        break;
    }
    default: {
        static const char body[] = "Internal Server Error";
        send_http_response(client_fd, "500 Internal Server Error", "text/plain; charset=utf-8",
                           body, sizeof(body) - 1, NULL);
        break;
    }
    }
}

static const char *trim_whitespace(char *s) {
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }

    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return s;
}

static ssize_t find_header_end(const unsigned char *data, size_t len) {
    if (len < 4) {
        return -1;
    }
    for (size_t i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' &&
            data[i + 3] == '\n') {
            return (ssize_t)i;
        }
    }
    return -1;
}

static bool parse_request_headers(const unsigned char *raw,
                                  size_t header_len,
                                  HttpRequest *request,
                                  int *status_code) {
    char *header_text = (char *)malloc(header_len + 1);
    if (header_text == NULL) {
        *status_code = 500;
        return false;
    }

    memcpy(header_text, raw, header_len);
    header_text[header_len] = '\0';

    char *line = header_text;
    char *line_end = strstr(line, "\r\n");
    if (line_end == NULL) {
        free(header_text);
        *status_code = 400;
        return false;
    }
    *line_end = '\0';

    if (sscanf(line, "%7s %255s", request->method, request->path) != 2) {
        free(header_text);
        *status_code = 400;
        return false;
    }

    char *query = strchr(request->path, '?');
    if (query != NULL) {
        *query = '\0';
    }

    char *cursor = line_end + 2;
    while (*cursor != '\0') {
        char *next = strstr(cursor, "\r\n");
        if (next != NULL) {
            *next = '\0';
        }

        if (*cursor == '\0') {
            break;
        }

        if (strncasecmp(cursor, "Content-Length:", 15) == 0) {
            char *value = (char *)trim_whitespace(cursor + 15);
            errno = 0;
            char *end = NULL;
            unsigned long parsed = strtoul(value, &end, 10);
            const char *tail = trim_whitespace(end != NULL ? end : value);
            if (errno != 0 || end == value || *tail != '\0') {
                free(header_text);
                *status_code = 400;
                return false;
            }
            request->content_length = (size_t)parsed;
        } else if (strncasecmp(cursor, "Content-Type:", 13) == 0) {
            const char *value = trim_whitespace(cursor + 13);
            snprintf(request->content_type, sizeof(request->content_type), "%s", value);
        }

        if (next == NULL) {
            break;
        }
        cursor = next + 2;
    }

    free(header_text);
    return true;
}

bool read_http_request(int client_fd, HttpRequest *request, int *status_code) {
    memset(request, 0, sizeof(*request));
    unsigned char header_buffer[MAX_HEADER_SIZE];
    size_t total_read = 0;
    size_t header_end = SIZE_MAX;

    while (total_read < sizeof(header_buffer)) {
        ssize_t n = read(client_fd, header_buffer + total_read, sizeof(header_buffer) - total_read);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("read");
            *status_code = 400;
            return false;
        }
        if (n == 0) {
            break;
        }
        total_read += (size_t)n;

        ssize_t idx = find_header_end(header_buffer, total_read);
        if (idx >= 0) {
            header_end = (size_t)idx;
            break;
        }
    }

    if (header_end == SIZE_MAX) {
        *status_code = 400;
        return false;
    }

    if (!parse_request_headers(header_buffer, header_end, request, status_code)) {
        return false;
    }

    if (request->content_length > MAX_REQUEST_SIZE) {
        *status_code = 413;
        return false;
    }

    size_t body_start = header_end + 4;
    size_t initial_body_bytes = total_read > body_start ? total_read - body_start : 0;
    request->body_length = request->content_length;

    if (request->body_length == 0) {
        request->body = NULL;
        return true;
    }

    request->body = (unsigned char *)malloc(request->body_length);
    if (request->body == NULL) {
        *status_code = 500;
        return false;
    }

    if (initial_body_bytes > request->body_length) {
        initial_body_bytes = request->body_length;
    }
    if (initial_body_bytes > 0) {
        memcpy(request->body, header_buffer + body_start, initial_body_bytes);
    }

    size_t copied = initial_body_bytes;
    while (copied < request->body_length) {
        ssize_t n = read(client_fd, request->body + copied, request->body_length - copied);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("read");
            *status_code = 400;
            free(request->body);
            request->body = NULL;
            request->body_length = 0;
            return false;
        }
        if (n == 0) {
            break;
        }
        copied += (size_t)n;
    }

    if (copied < request->body_length) {
        *status_code = 400;
        free(request->body);
        request->body = NULL;
        request->body_length = 0;
        return false;
    }

    return true;
}

void free_http_request(HttpRequest *request) {
    free(request->body);
    request->body = NULL;
    request->body_length = 0;
}
