# Load Test (load_test.c) — Learnable Guide

This document explains how the load-test client in `src/load_test.c` works. It demonstrates **client-side TCP**, **name resolution with getaddrinfo**, **multithreading with pthreads**, and **atomic counters** for thread-safe statistics.

---

## 1. What the load test does

- Takes optional arguments: `[host] [port] [total_connections] [concurrency]`.
- Spawns **concurrency** worker threads.
- Each worker repeatedly grabs the next “connection id” (0 to total_connections - 1) and runs **one** HTTP request: connect → send GET → read response → close.
- When all requests are done, it prints elapsed time, success/failure counts, success rate, connections per second, and total response bytes.

So we send **total_connections** HTTP requests, with at most **concurrency** in flight at once.

---

## 2. Concepts you need

- **getaddrinfo**: Resolves hostname + port to one or more socket addresses (IPv4/IPv6). Prefer it over raw `gethostbyname`; it’s reentrant and supports IPv6.
- **pthreads**: One function runs as the “worker”; we create many threads and pass a shared context. We use **atomics** so threads can update counts without a mutex.
- **HTTP client**: We send a minimal GET request and read until the server closes the connection (Connection: close).

---

## 3. Program flow (big picture)

```
parse_args(host, port, total_connections, concurrency)
→ allocate pthread_t array and shared atomics
→ start timer
→ create concurrency threads, each running worker_main
→ join all threads
→ stop timer, print results
```

Each thread in `worker_main`:

- Atomically take the next connection id (`atomic_fetch_add`).
- If id >= total_connections, exit.
- Otherwise: connect → send_request → read_response → update success/failure and response_bytes (via atomics).

---

## 4. Configuration and context structs

```c
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
```

- **LoadTestConfig**: Read-only config; same for all threads.
- **WorkerContext**: Passed to every worker. All workers share the same atomics so they can safely increment `next_connection`, `success_count`, `failure_count`, and `response_bytes`.

---

## 5. Argument parsing

```c
static LoadTestConfig parse_args(int argc, char **argv) {
    cfg.host = (argc > 1) ? argv[1] : DEFAULT_HOST;
    cfg.port = (argc > 2) ? argv[2] : DEFAULT_PORT;
    cfg.total_connections = (argc > 3) ? parse_long(argv[3], ...) : DEFAULT_TOTAL_CONNECTIONS;
    cfg.concurrency = (argc > 4) ? parse_long(argv[4], ...) : DEFAULT_CONCURRENCY;
    if (cfg.concurrency > cfg.total_connections)
        cfg.concurrency = cfg.total_connections;
    return cfg;
}
```

- **parse_long**: Uses `strtol` and checks that the whole string was consumed and value &gt; 0.
- Concurrency is capped at total_connections so we don’t create more threads than we need.

---

## 6. Connecting to the server: getaddrinfo + connect

```c
static int connect_to_server(const char *host, const char *port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int gai_status = getaddrinfo(host, port, &hints, &res);
    ...
}
```

- **AF_UNSPEC**: Allow IPv4 or IPv6; `getaddrinfo` will return whatever the system has for that host/port.
- **SOCK_STREAM**: TCP. We don’t set `ai_protocol`; the implementation will choose TCP for stream.

Then we iterate the linked list and try each address:

```c
for (struct addrinfo *it = res; it != NULL; it = it->ai_next) {
    sock_fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    ...
    setsockopt(..., SO_RCVTIMEO, ...);
    setsockopt(..., SO_SNDTIMEO, ...);
    if (connect(sock_fd, it->ai_addr, it->ai_addrlen) == 0)
        break;
    close(sock_fd);
    sock_fd = -1;
}
freeaddrinfo(res);
return sock_fd;
```

- **Why loop?** A hostname can resolve to multiple addresses (e.g. IPv4 and IPv6, or multiple A records). We try each until one succeeds.
- **Timeouts**: SO_RCVTIMEO and SO_SNDTIMEO (5 seconds here) prevent a stuck read/write from blocking a worker forever.
- **freeaddrinfo**: Required; `getaddrinfo` allocates the list.

---

## 7. Sending the HTTP request

```c
static bool send_request(int sock_fd, const char *host) {
    char request[256];
    int request_len = snprintf(request, sizeof(request),
        "GET / HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        host);
    ...
    while (sent < (size_t)request_len) {
        ssize_t n = write(sock_fd, request + sent, (size_t)request_len - sent);
        ...
        sent += (size_t)n;
    }
    return true;
}
```

- **Host header**: HTTP/1.1 requires it. We use the same host we connected to.
- **Connection: close**: Tells the server we’re done after one response and it will close the socket, so we can read until EOF.
- **Partial writes**: Same pattern as the server: loop until all bytes are written, retry on EINTR.

---

## 8. Reading the response

```c
static bool read_response(int sock_fd, long *bytes_read_out) {
    char buffer[RESPONSE_BUFFER_SIZE];
    bool saw_200 = false;
    long total = 0;
    for (;;) {
        ssize_t n = read(sock_fd, buffer, sizeof(buffer));
        if (n <= 0) ...
        total += (long)n;
        if (!saw_200 && (strstr(buffer, "HTTP/1.1 200") != NULL || ...))
            saw_200 = true;
    }
    *bytes_read_out = total;
    return saw_200 && total > 0;
}
```

- We read until the server closes the connection (read returns 0).
- “Success” here means: we saw a 200 status line and read at least one byte. This is a minimal check; we don’t parse headers or body.
- **bytes_read_out**: Used later to sum total response bytes across all connections (via atomics).

---

## 9. One connection end-to-end

```c
static bool run_single_connection(const LoadTestConfig *cfg, long *bytes_read) {
    int sock_fd = connect_to_server(cfg->host, cfg->port);
    if (sock_fd < 0) return false;
    bool ok = send_request(sock_fd, cfg->host);
    if (ok) ok = read_response(sock_fd, bytes_read);
    close(sock_fd);
    return ok;
}
```

Connect → send GET → read response → close. Any failure (connect, send, or read) is reported as a failed connection.

---

## 10. Worker thread and atomics

```c
static void *worker_main(void *arg) {
    WorkerContext *ctx = (WorkerContext *)arg;
    for (;;) {
        long id = atomic_fetch_add(ctx->next_connection, 1);
        if (id >= ctx->cfg->total_connections)
            break;

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
```

- **atomic_fetch_add(ptr, 1)**: Atomically adds 1 to `*ptr` and returns the **old** value. So each thread gets a unique id (0, 1, 2, …) until we reach total_connections.
- **Why atomics?** Multiple threads update the same counters. With plain `success_count++` you’d have a data race. `atomic_fetch_add` is lock-free and safe.
- No mutex is needed for these counters; atomics are enough for “increment and sometimes read” from multiple threads.

---

## 11. Main: creating and joining threads

```c
pthread_t *threads = calloc((size_t)cfg.concurrency, sizeof(*threads));
atomic_long next_connection = 0;
atomic_long success_count = 0;
atomic_long failure_count = 0;
atomic_long response_bytes = 0;

WorkerContext ctx = { .cfg = &cfg, .next_connection = &next_connection, ... };

double start = monotonic_seconds();
for (long i = 0; i < cfg.concurrency; ++i)
    pthread_create(&threads[i], NULL, worker_main, &ctx);
for (long i = 0; i < cfg.concurrency; ++i)
    pthread_join(threads[i], NULL);
double end = monotonic_seconds();
```

- **One context struct**: All threads get the same `&ctx` (same atomics). That’s correct because we only share pointers to atomics and the read-only config.
- **pthread_join**: Blocks until that thread’s `worker_main` returns. We join all threads before printing results so all counts are final.
- **monotonic_seconds()**: Uses `clock_gettime(CLOCK_MONOTONIC, ...)` so the elapsed time isn’t affected by system clock changes (e.g. NTP).

---

## 12. Results and exit code

```c
long success = atomic_load(&success_count);
long failure = atomic_load(&failure_count);
...
printf("Success rate: %.2f%%\n", (double)success * 100.0 / (double)cfg.total_connections);
printf("Connections/sec: %.2f\n", (double)cfg.total_connections / elapsed);
```

- **Exit code**: Returns `EXIT_FAILURE` if any connection failed, so scripts can detect load-test failures.

---

## 13. Summary table

| Topic            | In this program |
|------------------|------------------|
| Name resolution  | `getaddrinfo(host, port, &hints, &res)` then try each address with `connect()`. |
| Timeouts         | `SO_RCVTIMEO` / `SO_SNDTIMEO` so one slow connection doesn’t block a worker forever. |
| HTTP client      | Send minimal GET with Host and Connection: close; read until EOF. |
| Concurrency      | One pthread per “worker”; workers pull work via `atomic_fetch_add(next_connection, 1)`. |
| Shared state     | `atomic_long` for next_connection, success_count, failure_count, response_bytes. |
| Timing           | `clock_gettime(CLOCK_MONOTONIC)` before/after all work for wall-clock elapsed time. |

---

## 14. Relation to the server

- The **server** is now modular (`main.c`, `http.c`, `router.c`, `static_assets.c`) and still accepts one connection at a time. For `GET /`, it serves frontend HTML from the static asset cache and returns `200`.
- The **load test** opens many connections (possibly concurrent), sends a valid `GET /` request, and checks for a `200` while tracking bytes read. Together they form a minimal but complete client-server pair for observing throughput and failure behavior.

For the server’s design and socket lifecycle, see [SERVER.md](SERVER.md).
