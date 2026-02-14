#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

#define DEFAULT_PORT 8080
#define BACKLOG 128
#define MAX_REQUEST_SIZE (3 * 1024 * 1024)
#define MAX_FRAME_SIZE (2 * 1024 * 1024)
#define MAX_ASSET_PATH_SIZE 1024
#define MAX_HEADER_SIZE 16384

#ifndef WEB_ROOT_DIR
#define WEB_ROOT_DIR "web"
#endif

#endif
