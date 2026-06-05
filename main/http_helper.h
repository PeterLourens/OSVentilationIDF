/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file http_helper.h
 * @brief Header for HTTP helper utilities: TAR response headers, atomic JSON writes, and log formatting.
 */

#ifndef HTTP_HELPER_H
#define HTTP_HELPER_H

#include <stdarg.h>
#include <cJSON.h>
#include <driver/i2c_master.h>
#include <esp_attr.h>

#include "globals.h"
#include "general.h"

/**
 * @brief TAR header structure used for HTTP file downloads.
 */
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

// Function declarations
/**
 * @brief Remove ANSI escape sequences from a buffer.
 *
 * @param input Source string containing ANSI codes.
 * @param output Buffer to receive cleaned text.
 * @param len Number of bytes available in input.
 * @return size_t Number of bytes written to output, excluding the terminating null.
 */
size_t remove_ansi_escape_codes(const char *, char *, size_t);

/**
 * @brief WebSocket log printer used by the ESP log system.
 *
 * @param fmt Printf-style format string.
 * @param args Variable argument list.
 * @return int Number of characters formatted.
 */
int websocket_log_output(const char *, va_list);

/**
 * @brief UART log output helper that also writes to standard output.
 *
 * @param szFormat Printf-style format string.
 * @param args Variable argument list.
 * @return int Always returns 0 on success.
 */
int uart_log_output(const char *, va_list);

/**
 * @brief Atomically write a cJSON object to a file by writing a temporary file and renaming it.
 *
 * @param path Destination file path.
 * @param root cJSON object to serialize.
 * @return true if the write and rename succeeded, false otherwise.
 */
bool write_json_atomic_fs(const char *, cJSON *);

/**
 * @brief Compute the TAR checksum for a header block.
 *
 * @param hdr Pointer to the TAR header to checksum.
 */
void tar_fill_checksum(tar_header_t *hdr);

/**
 * @brief Write a TAR file header into the HTTP response stream.
 *
 * @param req HTTP request handle.
 * @param filename File name written into the TAR header.
 * @param filesize Size of the file in bytes.
 */
void tar_write_header(httpd_req_t *req, const char *filename, size_t filesize);

#endif