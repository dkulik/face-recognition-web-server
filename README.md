# Basic C Web Server

A minimal HTTP server and load-test client in plain C. This project is intended as a **learnable** codebase: it demonstrates TCP sockets, HTTP, signals, and multithreaded clients without frameworks.

## What’s in this repo

| Component   | Source           | Purpose |
|------------|------------------|---------|
| **Web server** | `src/main.c`     | Serves frontend assets from `web/` (`GET /`, `/styles.css`, `/app.js`) plus frame upload/download endpoints (`POST /api/frame`, `GET /api/frame`). |
| **Load test**  | `src/load_test.c`| Multithreaded client that opens many connections and reports success rate and throughput. |
| **Build**      | `CMakeLists.txt` | CMake config for both executables. |

## Quick start

```bash
# Build
mkdir -p build && cd build
cmake ..
make

# Terminal 1: start server (default port 8080)
./web_server

# Terminal 2: open in browser
# http://127.0.0.1:8080

# Terminal 3: optional load test
./load_test
```

See [Server usage](#server-usage) and [Load test usage](#load-test-usage) for options.

## Run tests

```bash
mkdir -p build && cd build
cmake ..
cmake --build .
ctest --output-on-failure
```

Current module-level tests:
- `test_http` (request parsing + response helpers)
- `test_static_assets` (asset loading/caching/serving)
- `test_router` (route behavior and `/api/frame` flow)

Run a single module test:

```bash
ctest -R test_http --output-on-failure
ctest -R test_static_assets --output-on-failure
ctest -R test_router --output-on-failure
```

## Presubmit check

Run the full presubmit locally:

```bash
./scripts/presubmit.sh
```

By default this uses an isolated build directory: `.build/presubmit`.

Enable automatic presubmit on `git push` (one-time setup per clone):

```bash
git config core.hooksPath .githooks
```

After enabling, the `pre-push` hook runs `./scripts/presubmit.sh` and blocks push if build/tests fail.

Automatic PR presubmit is configured in:

- `.github/workflows/pr-presubmit.yml`

It runs `./scripts/presubmit.sh` on every non-draft pull request update.
To enforce it before merge, enable branch protection on `main` and require the `PR Presubmit` status check.

---

## Learning path

1. **README.md** (this file) — Overview, build, and usage.
2. **[docs/SERVER.md](docs/SERVER.md)** — How the server works: socket lifecycle, `bind`/`listen`/`accept`, handling partial writes, and graceful shutdown with `SIGINT`.
3. **[docs/LOAD_TEST.md](docs/LOAD_TEST.md)** — How the load test works: `getaddrinfo`, worker threads, atomics, and a minimal HTTP client.

Recommended order: read the server doc first, then the load test doc.

---

## Server usage

```text
./web_server [port]
```

- **Default port:** 8080.
- **Example:** `./web_server 3000` → listen on port 3000.
- **Stop:** Ctrl+C (graceful shutdown).

The server binds to `0.0.0.0`, so it accepts connections from any interface.
Open `http://127.0.0.1:8080` in a browser to use the webcam relay page.

Quick endpoint checks:

```bash
curl http://127.0.0.1:8080
curl -X POST http://127.0.0.1:8080/api/frame -H "Content-Type: image/jpeg" --data-binary @frame.jpg
curl http://127.0.0.1:8080/api/frame --output returned.jpg
```

---

## Load test usage

```text
./load_test [host] [port] [total_connections] [concurrency]
```

| Argument            | Default   | Meaning |
|---------------------|-----------|---------|
| host                | 127.0.0.1 | Server hostname or IP. |
| port                | 8080      | Server port. |
| total_connections   | 1000      | Total HTTP requests to send. |
| concurrency         | 100       | Number of worker threads. |

**Examples:**

```bash
./load_test                              # 1000 requests, 100 concurrent
./load_test 127.0.0.1 8080 5000 50       # 5000 requests, 50 concurrent
./load_test myhost 3000 2000 200          # custom host/port and load
```

Start the web server first; the load test connects to it and prints elapsed time, success/failure counts, and connections per second.

---

## Requirements

- C11 compiler (e.g. GCC, Clang).
- CMake 3.16+.
- POSIX environment (Linux, macOS, etc.); the load test uses `pthreads`.

---

## Project layout

```text
.
├── .github/
│   └── workflows/
│       └── pr-presubmit.yml # CI presubmit on pull requests
├── .githooks/
│   └── pre-push          # Runs presubmit before push
├── CMakeLists.txt      # Build definition
├── README.md           # This file
├── docs/
│   ├── SERVER.md       # Server internals (learnable)
│   └── LOAD_TEST.md    # Load test internals (learnable)
├── scripts/
│   └── presubmit.sh    # Build + test gate
├── tests/
│   ├── test_http.c
│   ├── test_static_assets.c
│   ├── test_router.c
│   └── test_utils.h
├── web/
│   ├── index.html      # Frontend markup
│   ├── styles.css      # Frontend styles
│   └── app.js          # Frontend webcam + fetch logic
└── src/
    ├── main.c          # Server bootstrap + accept loop
    ├── http.c          # HTTP parsing + response utilities
    ├── http.h
    ├── router.c        # Route handling and frame relay logic
    ├── router.h
    ├── static_assets.c # Static asset cache/serving
    ├── static_assets.h
    ├── server_config.h # Shared server constants/config
    └── load_test.c     # Load test client
```
