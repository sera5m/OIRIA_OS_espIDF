#pragma once
// Vulcan web console — paste/drop .vul, wave generator, scope.
// Runs on head (tyrant/solo) AND secondary (puppet). Same firmware.
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts WiFi (STA from autoconnect, else AP "OIRIA-vulcan") + HTTP on :80.
 * Safe to call more than once. */
bool rs_vulcan_console_start(void);

/* Delayed boot hook (call from rs_dom_link_start_listen). */
void rs_vulcan_console_boot(void);

bool rs_vulcan_console_up(void);

#ifdef __cplusplus
}
#endif
