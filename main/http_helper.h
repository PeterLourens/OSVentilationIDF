#ifndef HTTP_HELPER_H
#define HTTP_HELPER_H

#include <cJSON.h>
#include <driver/i2c_master.h>
#include <miniz.h>

#include "globals.h"
#include "general.h"

// TAR header structure (512 bytes)
typedef struct
{
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
} tar_header_t;

typedef struct
{
    httpd_req_t *req;
    tdefl_compressor comp;
    uint8_t out[512];
} gzip_stream_t;

// Function declarations
size_t remove_ansi_escape_codes(const char *, char *, size_t);
int websocket_log_output(const char *, va_list);
int uart_log_output(const char *, va_list);
bool write_json_atomic_fs(const char *, cJSON *);
void tar_fill_checksum(tar_header_t *hdr);
void tar_write_header(httpd_req_t *req, const char *filename, size_t filesize);
void gzip_stream_init(gzip_stream_t *gs, httpd_req_t *req);
void gzip_stream_write(gzip_stream_t *gs, const void *data, size_t len);
void gzip_stream_finish(gzip_stream_t *gs);

#endif