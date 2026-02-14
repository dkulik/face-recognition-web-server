#ifndef ROUTER_H
#define ROUTER_H

#include "http.h"

void handle_request(int client_fd, const HttpRequest *request);

#endif
