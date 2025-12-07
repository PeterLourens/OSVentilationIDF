#ifndef HTTP_POST_FUNCTIONS_H
#define HTTP_POST_FUNCTIONS_H

#include "globals.h"
#include "general.h"

#define LOG_BUF_SIZE 256

// Function declarations
cJSON *build_bus_config_from_body_urlencoded(const char *, size_t, const char *);
cJSON *build_network_config_from_body_urlencoded(const char *, size_t);

#endif