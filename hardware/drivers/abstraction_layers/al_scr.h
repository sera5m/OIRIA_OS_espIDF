#ifndef AL_SCR_H_
#define AL_SCR_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    void (*display_framebuffer)(bool, bool);
    void (*refresh_screen)(void);
} ScreenDriver;

/* Public API */
void screen_set_driver(const ScreenDriver *driver);

void display_framebuffer(bool a, bool b);
void refreshScreen(void);

/* Available drivers */
extern const ScreenDriver onboard_screen_driver;
extern const ScreenDriver external_screen_driver;

#ifdef __cplusplus
}
#endif

#endif /* AL_SCR_H_ */