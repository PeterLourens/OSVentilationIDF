#ifndef HTTP_POST_FUNCTIONS_H
#define HTTP_POST_FUNCTIONS_H

#include "globals.h"
#include "general.h"

#include "cJSON.h"
#include "driver/i2c_master.h"

#define LOG_BUF_SIZE 256

// Function declarations
cJSON *build_bus_config_from_body_urlencoded(const char *, size_t, const char *);
cJSON *build_network_config_from_body_urlencoded(const char *, size_t);
cJSON *build_i2c_config_from_body_urlencoded(const char *, size_t);
cJSON *build_mqtt_config_from_body_urlencoded(const char *, size_t);
cJSON *build_influxdb_config_from_body_urlencoded(const char *, size_t);
cJSON *build_syslog_config_from_body_urlencoded(const char *, size_t);
void url_decode_inplace(char *);
cJSON *build_time_config_from_body_urlencoded(const char *, size_t);
cJSON *build_fan_config_from_body_urlencoded(const char *, size_t);
cJSON *build_state_gen_config_from_body_urlencoded(const char *, size_t);
cJSON *build_state_config_from_body_urlencoded(const char *, size_t, int);

#endif