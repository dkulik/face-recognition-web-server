#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT "8080"
#define DEFAULT_TOTAL_CONNECTIONS 1000L
#define DEFAULT_CONCURRENCY 100L
#define RESPONSE_BUFFER_SIZE 1024

typedef struct {
    const char *host;
    const char *port;
    long total_connections;
    long concurrency;
} LoadTestConfig;

typedef struct {
    const LoadTestConfig *cfg;
    atomic_long *next_connection;
    atomic_long *success_count;
    atomic_long *failure_count;
    atomic_long *response_bytes;
} WorkerContext;

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [host] [port] [total_connections] [concurrency]\n", prog);
    fprintf(stderr, "Defaults: host=%s port=%s total=%ld concurrency=%ld\n",
            DEFAULT_HOST, DEFAULT_PORT, DEFAULT_TOTAL_CONNECTIONS, DEFAULT_CONCURRENCY);
}

static long parse_long(const char *arg, const char *name) {
    char *end = NULL;
    long value = strtol(arg, &end, 10);
    if (end == arg || *end != '\0' || value <= 0) {
        fprintf(stderr, "Invalid %s: %s\n", name, arg);
        exit(EXIT_FAILURE);
    }
    return value;
}

static LoadTestConfig parse_args(int argc, char **argv) {
    LoadTestConfig cfg;
    cfg.host = (argc > 1) ? argv[1] : DEFAULT_HOST;
    cfg.port = (argc > 2) ? argv[2] : DEFAULT_PORT;
    cfg.total_connections =
        (argc > 3) ? parse_long(argv[3], "total_connections") : DEFAULT_TOTAL_CONNECTIONS;
    cfg.concurrency = (argc > 4) ? parse_long(argv[4], "concurrency") : DEFAULT_CONCURRENCY;

    if (argc > 5) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if (cfg.concurrency > cfg.total_connections) {
        cfg.concurrency = cfg.total_connections;
    }

    return cfg;
}

static int connect_to_server(const char *host, const char *port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int gai_status = getaddrinfo(host, port, &hints, &res);
    if (gai_status != 0) {
        return -1;
    }

    int sock_fd = -1;
    for (struct addrinfo *it = res; it != NULL; it = it->ai_next) {
        sock_fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (sock_fd < 0) {
            continue;
        }

        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        (void)setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        (void)setsockopt(sock_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        if (connect(sock_fd, it->ai_addr, it->ai_addrlen) == 0) {
            break;
        }

        close(sock_fd);
        sock_fd = -1;
    }

    freeaddrinfo(res);
    return sock_fd;
}

static bool send_request(int sock_fd, const char *host) {
    char request[256];
    int request_len = snprintf(request, sizeof(request),
                               "GET / HTTP/1.1\r\n"
                               "Host: %s\r\n"
                               "Connection: close\r\n"
                               "\r\n",
                               host);
    if (request_len <= 0 || request_len >= (int)sizeof(request)) {
        return false;
    }

    size_t sent = 0;
    while (sent < (size_t)request_len) {
        ssize_t n = write(sock_fd, request + sent, (size_t)request_len - sent);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        sent += (size_t)n;
    }
    return true;
}

static bool read_response(int sock_fd, long *bytes_read_out) {
    char buffer[RESPONSE_BUFFER_SIZE + 1];
    bool saw_200 = false;
    long total = 0;

    for (;;) {
        ssize_t n = read(sock_fd, buffer, RESPONSE_BUFFER_SIZE);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            break;
        }

        total += (long)n;
        buffer[n] = '\0';
        if (!saw_200 &&
            (strstr(buffer, "HTTP/1.1 200") != NULL || strstr(buffer, "HTTP/1.0 200") != NULL)) {
            saw_200 = true;
        }
    }

    *bytes_read_out = total;
    return saw_200 && total > 0;
}

static bool run_single_connection(const LoadTestConfig *cfg, long *bytes_read) {
    int sock_fd = connect_to_server(cfg->host, cfg->port);
    if (sock_fd < 0) {
        return false;
    }

    bool ok = send_request(sock_fd, cfg->host);
    if (ok) {
        ok = read_response(sock_fd, bytes_read);
    }

    close(sock_fd);
    return ok;
}

static void *worker_main(void *arg) {
    WorkerContext *ctx = (WorkerContext *)arg;

    for (;;) {
        long id = atomic_fetch_add(ctx->next_connection, 1);
        if (id >= ctx->cfg->total_connections) {
            break;
        }

        long bytes_read = 0;
        if (run_single_connection(ctx->cfg, &bytes_read)) {
            atomic_fetch_add(ctx->success_count, 1);
            atomic_fetch_add(ctx->response_bytes, bytes_read);
        } else {
            atomic_fetch_add(ctx->failure_count, 1);
        }
    }

    return NULL;
}

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

int main(int argc, char **argv) {
    LoadTestConfig cfg = parse_args(argc, argv);

    printf("Running load test against %s:%s\n", cfg.host, cfg.port);
    printf("Target connections: %ld, concurrency: %ld\n", cfg.total_connections, cfg.concurrency);

    pthread_t *threads = calloc((size_t)cfg.concurrency, sizeof(*threads));
    if (threads == NULL) {
        perror("calloc");
        return EXIT_FAILURE;
    }

    atomic_long next_connection = 0;
    atomic_long success_count = 0;
    atomic_long failure_count = 0;
    atomic_long response_bytes = 0;

    WorkerContext ctx = {
        .cfg = &cfg,
        .next_connection = &next_connection,
        .success_count = &success_count,
        .failure_count = &failure_count,
        .response_bytes = &response_bytes,
    };

    double start = monotonic_seconds();
    for (long i = 0; i < cfg.concurrency; ++i) {
        if (pthread_create(&threads[i], NULL, worker_main, &ctx) != 0) {
            perror("pthread_create");
            free(threads);
            return EXIT_FAILURE;
        }
    }

    for (long i = 0; i < cfg.concurrency; ++i) {
        (void)pthread_join(threads[i], NULL);
    }
    double end = monotonic_seconds();

    free(threads);

    long success = atomic_load(&success_count);
    long failure = atomic_load(&failure_count);
    long bytes = atomic_load(&response_bytes);
    double elapsed = end - start;
    if (elapsed <= 0.0) {
        elapsed = 0.000001;
    }

    printf("\nResults\n");
    printf("Elapsed time: %.3f sec\n", elapsed);
    printf("Successful connections: %ld\n", success);
    printf("Failed connections: %ld\n", failure);
    printf("Success rate: %.2f%%\n", ((double)success * 100.0) / (double)cfg.total_connections);
    printf("Connections/sec: %.2f\n", (double)cfg.total_connections / elapsed);
    printf("Response bytes read: %ld\n", bytes);

    return (failure == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
