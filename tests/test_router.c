#include "router.h"

#include "server_config.h"
#include "static_assets.h"
#include "test_utils.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static size_t run_route_and_read(const HttpRequest *request, char *response, size_t cap) {
    int fds[2];
    make_socket_pair(fds);

    handle_request(fds[0], request);
    shutdown(fds[0], SHUT_WR);

    size_t n = read_all_or_fail(fds[1], response, cap - 1);
    response[n] = '\0';
    close_pair(fds);
    return n;
}

static HttpRequest make_request(const char *method, const char *path) {
    HttpRequest request;
    memset(&request, 0, sizeof(request));
    snprintf(request.method, sizeof(request.method), "%s", method);
    snprintf(request.path, sizeof(request.path), "%s", path);
    return request;
}

static void test_router_static_route(void) {
    HttpRequest request = make_request("GET", "/");
    char response[8192];
    run_route_and_read(&request, response, sizeof(response));
    assert_contains(response, "HTTP/1.1 200 OK");
    assert_contains(response, "Content-Type: text/html; charset=utf-8");
}

static void test_router_frame_flow(void) {
    char response[4096];

    HttpRequest get_before_post = make_request("GET", "/api/frame");
    run_route_and_read(&get_before_post, response, sizeof(response));
    assert_contains(response, "HTTP/1.1 204 No Content");

    HttpRequest bad_method = make_request("PUT", "/api/frame");
    run_route_and_read(&bad_method, response, sizeof(response));
    assert_contains(response, "HTTP/1.1 405 Method Not Allowed");

    HttpRequest post_empty = make_request("POST", "/api/frame");
    post_empty.body_length = 0;
    run_route_and_read(&post_empty, response, sizeof(response));
    assert_contains(response, "HTTP/1.1 400 Bad Request");

    HttpRequest post_too_large = make_request("POST", "/api/frame");
    post_too_large.body = (unsigned char *)"x";
    post_too_large.body_length = (size_t)MAX_FRAME_SIZE + 1;
    run_route_and_read(&post_too_large, response, sizeof(response));
    assert_contains(response, "HTTP/1.1 413 Payload Too Large");

    unsigned char frame_data[] = "abc123";
    HttpRequest post_ok = make_request("POST", "/api/frame");
    post_ok.body = frame_data;
    post_ok.body_length = sizeof(frame_data) - 1;
    run_route_and_read(&post_ok, response, sizeof(response));
    assert_contains(response, "HTTP/1.1 200 OK");
    assert_contains(response, "{\"ok\":true}");

    HttpRequest get_after_post = make_request("GET", "/api/frame");
    run_route_and_read(&get_after_post, response, sizeof(response));
    assert_contains(response, "HTTP/1.1 200 OK");
    assert_contains(response, "Content-Type: image/jpeg");
    assert_contains(response, "abc123");
}

static void test_router_not_found(void) {
    HttpRequest request = make_request("GET", "/missing");
    char response[2048];
    run_route_and_read(&request, response, sizeof(response));
    assert_contains(response, "HTTP/1.1 404 Not Found");
}

int main(void) {
    assert(load_static_assets());
    test_router_static_route();
    test_router_frame_flow();
    test_router_not_found();
    free_static_assets();
    puts("test_router: OK");
    return 0;
}
