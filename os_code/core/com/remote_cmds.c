#include "remote_cmds.h"
#include "boot_mode_select.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "remote";

static void reply(const char *s)
{
#if defined(CONFIG_ESP_CONSOLE_UART_NUM)
    uart_write_bytes((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, s, strlen(s));
#else
    (void)s;
#endif
}

// Runtime commands the phone link sends.
// /bootseq ... is handled by the early path; we forward it.
// /sensor accel|temp|hr|spo2 returns a one-line reading or "na".
bool remote_handle_line(const char *line)
{
    if (!line || line[0] != '/') return false;

    if (strncasecmp(line, "/bootseq", 8) == 0) {
        boot_role_t r = BOOT_ROLE_SOLO;
        boot_serial_handle_line(line, &r);
        return true;
    }

    if (strncasecmp(line, "/sensor", 7) == 0) {
        const char *p = line + 7;
        while (*p == ' ') p++;

        // Drivers are not globally linked yet. Reply with the
        // shape the phone expects so the path is real.
        if (strncasecmp(p, "accel", 5) == 0) {
            reply("sensor accel x=na y=na z=na\r\n");
        } else if (strncasecmp(p, "temp", 4) == 0) {
            reply("sensor temp c=na\r\n");
        } else if (strncasecmp(p, "hr", 2) == 0) {
            reply("sensor hr bpm=na\r\n");
        } else if (strncasecmp(p, "spo2", 4) == 0) {
            reply("sensor spo2 pct=na\r\n");
        } else {
            reply("err: /sensor accel|temp|hr|spo2\r\n");
        }
        return true;
    }

    return false;
}
