# Web Server — Learnable Guide (Modular Version)

This document explains the current server architecture after refactoring.  
The server is still intentionally small and single-threaded, but now split into focused modules for readability and testing.

---

## 1. What the server does

- Serves frontend files from `web/`:
  - `GET /` -> `index.html`
  - `GET /styles.css`
  - `GET /app.js`
- Accepts uploaded webcam frames:
  - `POST /api/frame` (expects bytes, typically `image/jpeg`)
- Returns most recent frame:
  - `GET /api/frame` (`204` until first frame arrives, then `200 image/jpeg`)

---

## 2. Module map

| Module | Files | Responsibility |
|---|---|---|
| Bootstrap/socket loop | `src/main.c` | Parse port, set signal handler, create listening socket, accept connections, drive request lifecycle. |
| HTTP layer | `src/http.h`, `src/http.c` | Parse HTTP request line/headers/body, send HTTP responses, send standard error responses. |
| Static assets | `src/static_assets.h`, `src/static_assets.c` | Load `web/` assets at startup, cache in memory, serve by route. |
| Router | `src/router.h`, `src/router.c` | Route matching and endpoint behavior (`/api/frame`, static fallback, 404/405). |
| Shared config | `src/server_config.h` | Central constants (`BACKLOG`, `MAX_FRAME_SIZE`, etc.). |

---

## 3. Runtime flow

```text
main
-> parse_port()
-> install SIGINT handler
-> create_listening_socket()
-> load_static_assets()
-> loop:
   -> accept()
   -> read_http_request()
   -> handle_request()
   -> free_http_request()
   -> close(client)
-> free_static_assets()
-> close(server)
```

The server handles one connection at a time. This keeps the code simple and is enough for learning and local demos.

---

## 4. Key constants

From `src/server_config.h`:

- `DEFAULT_PORT 8080`
- `BACKLOG 128`
- `MAX_REQUEST_SIZE 3MB`
- `MAX_FRAME_SIZE 2MB`
- `MAX_HEADER_SIZE 16KB`

These limits protect memory and bound request parsing.

---

## 5. HTTP parsing model

`read_http_request()` in `src/http.c`:

1. Reads into a fixed header buffer until `\r\n\r\n`.
2. Parses request line + headers (`method`, `path`, `Content-Length`, `Content-Type`).
3. Allocates only the needed body size (`Content-Length`) if non-zero.
4. Reads remaining body bytes.
5. Returns status code (`400`, `413`, `500`) on parse/read failures.

Important: query strings are stripped from `path` (e.g., `/styles.css?x=1` -> `/styles.css`).

---

## 6. Static asset strategy

`load_static_assets()` reads configured frontend files once at startup and keeps bytes in memory.

Benefits:
- No per-request disk I/O for static files.
- Simpler serving path in `serve_static_asset()`.

If assets fail to load, server startup fails fast.

---

## 7. Frame relay behavior

`src/router.c` keeps an in-memory `latest_frame` buffer:

- `POST /api/frame`:
  - rejects empty body (`400`)
  - rejects bodies larger than `MAX_FRAME_SIZE` (`413`)
  - copies bytes into `latest_frame`
  - returns `{"ok":true}`
- `GET /api/frame`:
  - returns `204` if no frame yet
  - otherwise returns current frame bytes as `image/jpeg`

This is an in-memory, last-frame-only relay by design.

---

## 8. Error handling

`send_error_response()` centralizes standard responses:

- `400 Bad Request`
- `404 Not Found`
- `405 Method Not Allowed`
- `413 Payload Too Large`
- `500 Internal Server Error`

All responses include `Connection: close`.

---

## 9. Testing

Module tests live in `tests/` and run via CTest:

- `test_http`
- `test_static_assets`
- `test_router`

Run:

```bash
mkdir -p build && cd build
cmake ..
cmake --build .
ctest --output-on-failure
```

---

## 10. Current limitations (intentional)

- Single-threaded request handling.
- No TLS/HTTPS.
- No full HTTP feature set (chunked transfer, keep-alive pipelining, etc.).
- Frame store is process-local memory (no persistence, no multi-instance sync).

For this project’s goals, these tradeoffs keep the implementation compact and inspectable.
