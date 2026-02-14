#include "static_assets.h"

#include "http.h"
#include "server_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *url_path;
    const char *file_name;
    const char *content_type;
    unsigned char *contents;
    size_t contents_len;
} StaticAsset;

static StaticAsset static_assets[] = {
    {"/", "index.html", "text/html; charset=utf-8", NULL, 0},
    {"/styles.css", "styles.css", "text/css; charset=utf-8", NULL, 0},
    {"/app.js", "app.js", "application/javascript; charset=utf-8", NULL, 0},
};

static bool read_file_bytes(const char *path, unsigned char **data_out, size_t *len_out) {
    *data_out = NULL;
    *len_out = 0;

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    long file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return false;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    if (file_size == 0) {
        fclose(file);
        return true;
    }

    unsigned char *buffer = (unsigned char *)malloc((size_t)file_size);
    if (buffer == NULL) {
        fclose(file);
        return false;
    }

    size_t read_count = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);

    if (read_count != (size_t)file_size) {
        free(buffer);
        return false;
    }

    *data_out = buffer;
    *len_out = (size_t)file_size;
    return true;
}

void free_static_assets(void) {
    size_t asset_count = sizeof(static_assets) / sizeof(static_assets[0]);
    for (size_t i = 0; i < asset_count; i++) {
        free(static_assets[i].contents);
        static_assets[i].contents = NULL;
        static_assets[i].contents_len = 0;
    }
}

bool load_static_assets(void) {
    size_t asset_count = sizeof(static_assets) / sizeof(static_assets[0]);
    for (size_t i = 0; i < asset_count; i++) {
        StaticAsset *asset = &static_assets[i];
        char asset_path[MAX_ASSET_PATH_SIZE];
        int n = snprintf(asset_path, sizeof(asset_path), "%s/%s", WEB_ROOT_DIR, asset->file_name);
        if (n < 0 || (size_t)n >= sizeof(asset_path)) {
            fprintf(stderr, "Asset path too long for %s\n", asset->file_name);
            free_static_assets();
            return false;
        }
        if (!read_file_bytes(asset_path, &asset->contents, &asset->contents_len)) {
            fprintf(stderr, "Failed to read static asset: %s\n", asset_path);
            free_static_assets();
            return false;
        }
    }
    return true;
}

bool serve_static_asset(int client_fd, const char *path) {
    size_t asset_count = sizeof(static_assets) / sizeof(static_assets[0]);
    for (size_t i = 0; i < asset_count; i++) {
        const StaticAsset *asset = &static_assets[i];
        if (strcmp(path, asset->url_path) != 0) {
            continue;
        }

        if (asset->contents == NULL) {
            send_error_response(client_fd, 500);
            return true;
        }

        send_http_response(client_fd, "200 OK", asset->content_type, asset->contents,
                           asset->contents_len, "Cache-Control: no-store\r\n");
        return true;
    }

    return false;
}
