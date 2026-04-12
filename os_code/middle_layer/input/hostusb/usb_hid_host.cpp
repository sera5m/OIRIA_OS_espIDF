#include "EspUsbHost.h"
#include "EspUsbHost/EspUsbHost.h"
//one of these should work
#include "esp_log.h"

static const char* TAG = "USBHID";

EspUsbHost usbHost;   // one global host

// Mouse callbacks
void UsbMouseDevice::onMouseMove(int8_t dx, int8_t dy, int8_t scroll)
{
    InputEvent ev{};
    ev.source_device_type = HIDInputDeviceType::Mouse;
    ev.action = KeyAction::PositionDelta;
    ev.delta_x = dx;
    ev.delta_y = dy;
    ev.delta_z = scroll;
    ev.timestamp = (uint32_t)(esp_timer_get_time() / 1000);

    xQueueSend(ProcInputQueTarget, &ev, 0);
}

void UsbMouseDevice::onMouseButton(uint8_t button, bool pressed)
{
    // Map to your key system or add a new action type later
    InputEvent ev{};
    ev.source_device_type = HIDInputDeviceType::Mouse;
    ev.action = pressed ? KeyAction::Down : KeyAction::Lift;
    ev.key = 0x1000 + button;   // arbitrary mouse button codes, adjust as needed
    xQueueSend(ProcInputQueTarget, &ev, 0);
}

// Keyboard callback
void UsbKeyboardDevice::onKeyboardKey(uint8_t ascii, uint8_t keycode, uint8_t modifier)
{
    InputEvent ev{};
    ev.source_device_type = HIDInputDeviceType::Keyboard;
    ev.action = KeyAction::Tap;        // or distinguish press/release if library supports
    ev.key = keycode;                  // you can map to your KEY_xxx if wanted
    xQueueSend(ProcInputQueTarget, &ev, 0);

    ESP_LOGI(TAG, "USB KB: ascii=%c keycode=0x%02X mod=0x%02X", ascii ? ascii : '?', keycode, modifier);
}
//a huge thank you to tanaka masayuki, who wrote this code on his github which ihave cloned and clumsily adapted to my device
//i used grok to adapt this to my framework, so if it breaks, it's my fault for using grok while trying to work on 5 subsystems in a day