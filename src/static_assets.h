#ifndef STATIC_ASSETS_H
#define STATIC_ASSETS_H

#include <stdbool.h>

bool load_static_assets(void);
void free_static_assets(void);
bool serve_static_asset(int client_fd, const char *path);

#endif
