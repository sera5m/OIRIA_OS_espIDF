#include <cstdint>
#include <vector>
#include <memory>
#include <string>

#pragma once
#include "freertos/FreeRTOS.h"   // Keep FreeRTOS early
#include "freertos/queue.h"

#include "hardware/drivers/encoders/ky040_driver.hpp"
#include "code_stuff/types.h"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "hardware/drivers/generic/button_driver.hpp"
#include "os_code/middle_layer/input/hid_t.h"

// Forward declaration
extern QueueHandle_t ProcInputQueTarget;
extern bool usb_hid_enabled;  // ← Changed from 'bool usb_hid_enabled;' to 'extern'

// ===================================================================
// Key definitions (keep yours)
#define KEY_ENTER   0x23CE
#define KEY_BACK    0x232B
#define KEY_UP      0x2191
#define KEY_DOWN    0x2193
#define KEY_LEFT    0x2190
#define KEY_RIGHT   0x2192

// ===================================================================
// Enums
enum class KeyAction : uint8_t {
    Tap,
    Press = Tap,     // alias
    Hold,
    Release,         // ← Add this
    Repeat,          // ← Add this
    PositionDelta,
    Unknown
};

enum class HIDInputDeviceType : uint8_t {
    Button,
    Knob,
    Mouse,
    Keyboard,
    Virtual,
    Unknown
};

struct __attribute__((packed)) full_PositionPointer{
    int Xpos; int Ypos; int Zpos;
    bool Xdown; bool Ydown; bool Zdown;
    bool Xdim; bool Ydim; bool Zdim;
};

// ===================================================================
// Input Event
// ===================================================================
struct InputEvent {
    uint16_t           key = 0;
    KeyAction          action = KeyAction::Unknown;
    HIDInputDeviceType source_device_type = HIDInputDeviceType::Unknown;
    int32_t            delta = 0;
    uint32_t           timestamp = 0;
    HIDTarget target;
};

// ===================================================================
// USB HID Functions (defined in input_handler.cpp)
// ===================================================================
#ifdef __cplusplus
extern "C" {
#endif

void boot_hid_usb(bool enable);
void hid_send_key(uint8_t keycode, bool pressed);
void hid_send_mouse(int8_t dx, int8_t dy, uint8_t buttons);
bool is_usb_hid_enabled(void);

#ifdef __cplusplus
}
#endif

// ===================================================================
// Base Device
class Device {
public:
    virtual ~Device() = default;
    virtual void update() = 0;
    virtual void interact(Device& other) = 0;
    virtual HIDInputDeviceType getType() const = 0;
    virtual const char* getName() const = 0;
    virtual uint32_t getUpdateIntervalMs() const { return 10; }
    virtual bool hasButton() const { return false; }
};

// ButtonDevice class
class ButtonDevice : public Device {
public:
    struct Properties {
        uint16_t press_key;
        bool send_on_press = true;
        bool send_on_release = false;
    };

    Properties props;
    esp_err_t initialize(const button_config_t* cfg);
    ~ButtonDevice() override;

    void update() override;
    void interact(Device& other) override;

    HIDInputDeviceType getType() const override { return HIDInputDeviceType::Button; }
    const char* getName() const override { return "Button"; }
    bool hasButton() const override { return true; }

private:
    button_handle_t btn_handle = nullptr;
    static void pressCallback(void* user_ctx, bool pressed);
};

// KnobDevice class
class KnobDevice : public Device {
public:
    struct Properties {
        uint16_t cw_key  = KEY_DOWN;
        uint16_t ccw_key = KEY_UP;
        float    sensitivity = 1.0f;
        bool     send_delta_instead_of_keys = false;
    };

    Properties props;

private:
    ky040_handle_t ky_handle = nullptr;
    static void twistCallback(void* user_ctx, int delta);

public:
    esp_err_t initialize(const ky040_config_t* cfg);
    ~KnobDevice() override;

    void update() override;
    void interact(Device& other) override;

    HIDInputDeviceType getType() const override { return HIDInputDeviceType::Knob; }
    const char* getName() const override { return "Knob"; }
    bool hasButton() const override { return true; }
};

// DeviceManager class
class DeviceManager {
private:
    std::vector<std::unique_ptr<Device>> devices;

public:
    void addDevice(std::unique_ptr<Device> device);
    void updateAll();
    void removeDevice(const char* name);
    void listDevices();

    size_t getDeviceCount() const { return devices.size(); }
    const std::vector<std::unique_ptr<Device>>& getDevices() const { return devices; }
};

extern DeviceManager gDeviceManager;