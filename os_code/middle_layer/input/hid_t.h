#ifndef HID_TARGET_H
#define HID_TARGET_H

#include <stdint.h>

#ifdef __cplusplus
enum class HIDTarget : uint8_t {
    nothing,
    actAsUsbHID,
    wireless_hid,
    toTask,
    toTask_and_usbHid,
    debug_log,
    everything,
    toTaskAndDebug,
    toTaskAndStreamcore, //data streaming universal core
    toStreamCore,
    toStreamCoreAndDebug,
    count
};

//future revision make this a listed array item via bitmasking so we can make any amount of combos
#else
// C fallback – just a uint8_t type
typedef uint8_t HIDTarget;
#endif

#endif
//i really hate putting it here, but this is a circular dependency issue
//if anyone reads this in the future, 2026 is hell. i feel like we all are go nna die to the far riaght
//and it's not gonna be quick and easy like libcucks say. it'll be 2027: people see democracy is over and expansion in targets, '28 fucking KING you know who will sit onhis golden throne. i mean they're trying to turn the whitehouse into a bunker. you can see it in bills, and they're just straight up ignoring judges who say no,nobody gaf