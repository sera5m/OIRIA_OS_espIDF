#pragma once

#include "EspUsbHost.h"   // from the library
#include "input_handler.hpp"   // your base Device + InputEvent

// Forward declare if needed
class UsbMouseDevice;
class UsbKeyboardDevice;

// Global USB host instance (or make it a member)
extern EspUsbHost usbHost;   // we'll define it in .cpp

// ====================== USB Mouse Device ======================
class UsbMouseDevice : public Device {
public:
    HIDInputDeviceType getType() const override { return HIDInputDeviceType::Mouse; }
    const char* getName() const override { return "USB Mouse"; }

    void update() override { /* usbHost.task() is called from a dedicated task */ }
    void interact(Device& other) override {}

    // Callbacks will push events to your queue
    void onMouseMove(int8_t dx, int8_t dy, int8_t scroll);
    void onMouseButton(uint8_t button, bool pressed);
};

// ====================== USB Keyboard Device ======================
class UsbKeyboardDevice : public Device {
public:
    HIDInputDeviceType getType() const override { return HIDInputDeviceType::Keyboard; }
    const char* getName() const override { return "USB Keyboard"; }

    void update() override {}
    void interact(Device& other) override {}

    void onKeyboardKey(uint8_t ascii, uint8_t keycode, uint8_t modifier);
};
