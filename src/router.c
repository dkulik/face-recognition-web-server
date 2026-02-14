#include "router.h"

#include "server_config.h"
#include "static_assets.h"

#include <string.h>

static unsigned char latest_frame[MAX_FRAME_SIZE];
static size_t latest_frame_size = 0;

void handle_request(int client_fd, const HttpRequest *request) {
    if (strcmp(request->method, "GET") == 0) {
        if (serve_static_asset(client_fd, request->path)) {
            return;
        }
    }

    if (strcmp(request->path, "/api/frame") == 0 && strcmp(request->method, "POST") == 0) {
        if (request->body_length == 0) {
            send_error_response(client_fd, 400);
            return;
        }
        if (request->body_length > MAX_FRAME_SIZE) {
            send_error_response(client_fd, 413);
            return;
        }

        memcpy(latest_frame, request->body, request->body_length);
        latest_frame_size = request->body_length;

        static const char body[] = "{\"ok\":true}";
        send_http_response(client_fd, "200 OK", "application/json", body, sizeof(body) - 1,
                           "Cache-Control: no-store\r\n");
        return;
    }

    if (strcmp(request->path, "/api/frame") == 0 && strcmp(request->method, "GET") == 0) {
        if (latest_frame_size == 0) {
            send_http_response(client_fd, "204 No Content", "text/plain; charset=utf-8", NULL, 0,
                               "Cache-Control: no-store\r\n");
            return;
        }

        send_http_response(client_fd, "200 OK", "image/jpeg", latest_frame, latest_frame_size,
                           "Cache-Control: no-store\r\n");
        return;
    }

    if (strcmp(request->path, "/api/frame") == 0) {
        send_error_response(client_fd, 405);
        return;
    }

    send_error_response(client_fd, 404);
}
