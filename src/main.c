#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "http.h"
#include "router.h"
#include "server_config.h"
#include "static_assets.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static volatile sig_atomic_t keep_running = 1;

static void handle_sigint(int signum) {
    (void)signum;
    keep_running = 0;
}

static int parse_port(int argc, char **argv) {
    if (argc < 2) {
        return DEFAULT_PORT;
    }

    char *end = NULL;
    long port = strtol(argv[1], &end, 10);
    if (end == argv[1] || *end != '\0' || port < 1 || port > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    return (int)port;
}

static int create_listening_socket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

int main(int argc, char **argv) {
    int port = parse_port(argc, argv);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    int server_fd = create_listening_socket(port);
    if (server_fd < 0) {
        return EXIT_FAILURE;
    }

    if (!load_static_assets()) {
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("Server listening on http://0.0.0.0:%d\n", port);

    while (keep_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            break;
        }

        HttpRequest request;
        int status_code = 400;
        if (!read_http_request(client_fd, &request, &status_code)) {
            send_error_response(client_fd, status_code);
            free_http_request(&request);
            close(client_fd);
            continue;
        }

        handle_request(client_fd, &request);
        free_http_request(&request);
        close(client_fd);
    }

    free_static_assets();
    close(server_fd);
    puts("Server stopped.");
    return EXIT_SUCCESS;
}
