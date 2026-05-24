/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "http_helper.h"
#include "task_uart_sender.h"

// Used to concact two log lines of the log output
static bool ws_prefix_pending = false;
static char ws_prefix_buf[256]; // holds prefix until message arrives

size_t remove_ansi_escape_codes(const char *input, char *output, size_t len)
{
    const char *ptr_in = input;
    char *ptr_out = output;

    while (ptr_in < input + len)
    {
        if (*ptr_in == '\033')
        {
            // Skip until 'm' or end
            const char *esc_end = strchr(ptr_in, 'm');
            if (esc_end)
            {
                ptr_in = esc_end + 1;
            }
            else
            {
                break; // malformed escape, stop
            }
        }
        else
        {
            *ptr_out++ = *ptr_in++;
        }
    }

    *ptr_out = '\0';           // null terminate
    return (ptr_out - output); // number of chars written
}

int websocket_log_output(const char *fmt, va_list args)
{
    va_list uart_args;
    va_copy(uart_args, args);
    uart_log_vprintf(fmt, uart_args);
    va_end(uart_args);

    static char log_print_buffer[512];
    int ret = vsnprintf(log_print_buffer, sizeof(log_print_buffer), fmt, args);

    if (ret > 0)
    {
        // Strip ANSI codes for WebSocket
        static char clean_buf[512];
        remove_ansi_escape_codes(log_print_buffer, clean_buf, ret);

        // Detect prefix vs message
        if (!ws_prefix_pending && strstr(clean_buf, ":"))
        {
            // Likely the prefix line (ends with ":")
            strncpy(ws_prefix_buf, clean_buf, sizeof(ws_prefix_buf));
            ws_prefix_pending = true;
        }
        else if (ws_prefix_pending)
        {
            // Concatenate prefix + message
            char final_buf[768];
            int final_len = snprintf(final_buf, sizeof(final_buf), "%s %s", ws_prefix_buf, clean_buf);

            ws_async_send(final_buf, final_len);
            ws_prefix_pending = false; // reset
        }
    }
    return ret;
}

int uart_log_output(const char *szFormat, va_list args)
{
    static char log_print_buffer[512];
    int ret = vsnprintf(log_print_buffer, sizeof(log_print_buffer), szFormat, args);
    if (ret >= 0)
    {
        // Also copy console to UART
        printf("%.*s", ret, log_print_buffer);
    }
    return (0);
}

// atomic write helper — writes tmp -> rename
bool write_json_atomic_fs(const char *path, cJSON *root)
{
    if (!path || !root)
        return false;

    char tmp_path[128];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    if (sensor_cfg_mutex)
        xSemaphoreTake(sensor_cfg_mutex, portMAX_DELAY);

    FILE *f = fopen(tmp_path, "w");
    if (!f)
    {
        if (sensor_cfg_mutex)
            xSemaphoreGive(sensor_cfg_mutex);
        return false;
    }

    char *out = cJSON_PrintUnformatted(root);
    if (!out)
    {
        fclose(f);
        if (sensor_cfg_mutex)
            xSemaphoreGive(sensor_cfg_mutex);
        return false;
    }

    fwrite(out, 1, strlen(out), f);
    fclose(f);
    free(out);

    int r = rename(tmp_path, path);

    if (sensor_cfg_mutex)
        xSemaphoreGive(sensor_cfg_mutex);

    return (r == 0);
}

void tar_fill_checksum(tar_header_t *hdr)
{
    memset(hdr->chksum, ' ', 8);
    unsigned int sum = 0;
    unsigned char *p = (unsigned char *)hdr;
    for (int i = 0; i < 512; i++)
        sum += p[i];
    snprintf(hdr->chksum, 8, "%06o", sum);
    hdr->chksum[6] = '\0';
    hdr->chksum[7] = ' ';
}

// Public function to write a tar header for a file to the HTTP response
void tar_write_header(httpd_req_t *req, const char *filename, size_t filesize)
{
    tar_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));

    snprintf(hdr.name, sizeof(hdr.name), "%s", filename);
    snprintf(hdr.mode, sizeof(hdr.mode), "%07o", 0644);
    snprintf(hdr.uid, sizeof(hdr.uid), "%07o", 0);
    snprintf(hdr.gid, sizeof(hdr.gid), "%07o", 0);
    snprintf(hdr.size, sizeof(hdr.size), "%011o", (unsigned int)filesize);
    snprintf(hdr.mtime, sizeof(hdr.mtime), "%011o", (unsigned int)time(NULL));

    memcpy(hdr.magic, "ustar", 5);
    memcpy(hdr.version, "00", 2);

    hdr.typeflag = '0'; // regular file

    tar_fill_checksum(&hdr);

    httpd_resp_send_chunk(req, (const char *)&hdr, 512);
}
