

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
//struct EnvConfig;
//extern EnvConfig v_env;
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
    Hold,
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




struct __attribute__((packed)) full_PositionPointer{ //where is my position pointer? equivalent to mouse pointer
int Xpos; int Ypos; int Zpos;
bool Xdown; bool Ydown; bool Zdown; //selective for axial constraints. regular nav and clicks are only
bool Xdim; bool Ydim; bool Zdim; //does it even have these dimensions for this device descriptor

}; //stores this mouse pointer fot the mouse itself 



// ===================================================================
// Input Event
// ===================================================================
// Input Event
struct InputEvent {
    uint16_t           key = 0;
    KeyAction          action = KeyAction::Unknown;
    HIDInputDeviceType source_device_type = HIDInputDeviceType::Unknown;
    int32_t            delta = 0;
    uint32_t           timestamp = 0;
    HIDTarget target;  // ← No default here - set explicitly when creating events
};



// ===================================================================
// Base Device
class Device {
public:
    virtual ~Device() = default;

    virtual void update() = 0;                    // called every poll cycle
    virtual void interact(Device& other) = 0;     // future extension

    virtual HIDInputDeviceType getType() const = 0;
    virtual const char* getName() const = 0;

    // Common properties you can expand later
    virtual uint32_t getUpdateIntervalMs() const { return 10; }  // default 100Hz
    virtual bool hasButton() const { return false; }
};





//a simple button device. cry harder about indection, fuck off. 
class ButtonDevice : public Device {
public:
    struct Properties {
        uint16_t press_key;    // key to send on press
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









// ===================================================================
// KnobDevice - one-axis scroller with optional key binding
class KnobDevice : public Device {
public:
    struct Properties {
        uint16_t cw_key  = KEY_DOWN;   // what "clockwise" sends as Tap
        uint16_t ccw_key = KEY_UP;     // what "counter-clockwise" sends
     
        float    sensitivity = 1.0f;
        bool     send_delta_instead_of_keys = false;  // if true, use PositionDelta
    };

    Properties props;

private:
ky040_handle_t ky_handle = nullptr;

    // Declare static callbacks ONLY ONCE
    static void twistCallback(void* user_ctx, int delta);
    //static void buttonCallback(void* user_ctx, bool pressed);

public:
    esp_err_t initialize(const ky040_config_t* cfg);
    ~KnobDevice() override;

    void update() override;
    void interact(Device& other) override;

    HIDInputDeviceType getType() const override { return HIDInputDeviceType::Knob; }
    const char* getName() const override { return "Knob"; }
    bool hasButton() const override { return true; }   // KY-040 has switch
    


};

// ===================================================================
// DeviceManager
class DeviceManager {
private:
    std::vector<std::unique_ptr<Device>> devices;

public:
    void addDevice(std::unique_ptr<Device> device);
    void updateAll();
    // In DeviceManager class
    void removeDevice(const char* name);
    void listDevices();

    size_t getDeviceCount() const { return devices.size(); }
    const std::vector<std::unique_ptr<Device>>& getDevices() const { return devices; }
};
// Global DeviceManager - everyone can use it
extern DeviceManager gDeviceManager;