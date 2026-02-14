#include "http.h"
#include "server_config.h"

#include "test_utils.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_send_http_response(void) {
    int fds[2];
    make_socket_pair(fds);

    static const char body[] = "hello";
    send_http_response(fds[0], "200 OK", "text/plain", body, sizeof(body) - 1, NULL);
    shutdown(fds[0], SHUT_WR);

    char response[2048];
    size_t n = read_all_or_fail(fds[1], response, sizeof(response) - 1);
    response[n] = '\0';

    assert_contains(response, "HTTP/1.1 200 OK");
    assert_contains(response, "Content-Type: text/plain");
    assert_contains(response, "Content-Length: 5");
    assert_contains(response, "\r\n\r\nhello");

    close_pair(fds);
}

static void test_send_error_response(void) {
    int fds[2];
    make_socket_pair(fds);

    send_error_response(fds[0], 404);
    shutdown(fds[0], SHUT_WR);

    char response[2048];
    size_t n = read_all_or_fail(fds[1], response, sizeof(response) - 1);
    response[n] = '\0';

    assert_contains(response, "HTTP/1.1 404 Not Found");
    assert_contains(response, "\r\n\r\nNot Found");

    close_pair(fds);
}

static void test_read_http_request_get(void) {
    int fds[2];
    make_socket_pair(fds);

    static const char req[] =
        "GET /styles.css?cache=1 HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";
    write_all_or_fail(fds[1], req, sizeof(req) - 1);
    shutdown(fds[1], SHUT_WR);

    HttpRequest request;
    int status = 0;
    bool ok = read_http_request(fds[0], &request, &status);

    assert(ok);
    assert(strcmp(request.method, "GET") == 0);
    assert(strcmp(request.path, "/styles.css") == 0);
    assert(request.body == NULL);
    assert(request.body_length == 0);
    free_http_request(&request);

    close_pair(fds);
}

static void test_read_http_request_post(void) {
    int fds[2];
    make_socket_pair(fds);

    static const char req_part1[] =
        "POST /api/frame HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "ab";
    static const char req_part2[] = "cde";
    write_all_or_fail(fds[1], req_part1, sizeof(req_part1) - 1);
    write_all_or_fail(fds[1], req_part2, sizeof(req_part2) - 1);
    shutdown(fds[1], SHUT_WR);

    HttpRequest request;
    int status = 0;
    bool ok = read_http_request(fds[0], &request, &status);

    assert(ok);
    assert(strcmp(request.method, "POST") == 0);
    assert(strcmp(request.path, "/api/frame") == 0);
    assert(strcmp(request.content_type, "image/jpeg") == 0);
    assert(request.body_length == 5);
    assert(memcmp(request.body, "abcde", 5) == 0);
    free_http_request(&request);

    close_pair(fds);
}

static void test_read_http_request_invalid_content_length(void) {
    int fds[2];
    make_socket_pair(fds);

    static const char req[] =
        "POST /api/frame HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: x\r\n"
        "\r\n";
    write_all_or_fail(fds[1], req, sizeof(req) - 1);
    shutdown(fds[1], SHUT_WR);

    HttpRequest request;
    int status = 0;
    bool ok = read_http_request(fds[0], &request, &status);

    assert(!ok);
    assert(status == 400);
    free_http_request(&request);

    close_pair(fds);
}

static void test_read_http_request_too_large(void) {
    int fds[2];
    make_socket_pair(fds);

    char req[256];
    int n = snprintf(
        req,
        sizeof(req),
        "POST /api/frame HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: %zu\r\n"
        "\r\n",
        (size_t)MAX_REQUEST_SIZE + 1);
    assert(n > 0);
    assert((size_t)n < sizeof(req));

    write_all_or_fail(fds[1], req, (size_t)n);
    shutdown(fds[1], SHUT_WR);

    HttpRequest request;
    int status = 0;
    bool ok = read_http_request(fds[0], &request, &status);

    assert(!ok);
    assert(status == 413);
    free_http_request(&request);

    close_pair(fds);
}

int main(void) {
    test_send_http_response();
    test_send_error_response();
    test_read_http_request_get();
    test_read_http_request_post();
    test_read_http_request_invalid_content_length();
    test_read_http_request_too_large();
    puts("test_http: OK");
    return 0;
}
