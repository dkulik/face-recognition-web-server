#include "static_assets.h"

#include "test_utils.h"

#include <assert.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_load_and_serve_asset(void) {
    assert(load_static_assets());

    int fds[2];
    make_socket_pair(fds);

    bool served = serve_static_asset(fds[0], "/styles.css");
    assert(served);
    shutdown(fds[0], SHUT_WR);

    char response[8192];
    size_t n = read_all_or_fail(fds[1], response, sizeof(response) - 1);
    response[n] = '\0';

    assert_contains(response, "HTTP/1.1 200 OK");
    assert_contains(response, "Content-Type: text/css; charset=utf-8");
    assert_contains(response, "body {");

    close_pair(fds);
    free_static_assets();
}

static void test_unknown_route_not_served(void) {
    assert(load_static_assets());
    bool served = serve_static_asset(1, "/does-not-exist");
    assert(!served);
    free_static_assets();
}

static void test_serve_when_not_loaded_returns_500(void) {
    free_static_assets();

    int fds[2];
    make_socket_pair(fds);

    bool served = serve_static_asset(fds[0], "/");
    assert(served);
    shutdown(fds[0], SHUT_WR);

    char response[2048];
    size_t n = read_all_or_fail(fds[1], response, sizeof(response) - 1);
    response[n] = '\0';
    assert_contains(response, "HTTP/1.1 500 Internal Server Error");

    close_pair(fds);
}

int main(void) {
    test_load_and_serve_asset();
    test_unknown_route_not_served();
    test_serve_when_not_loaded_returns_500();
    puts("test_static_assets: OK");
    return 0;
}
