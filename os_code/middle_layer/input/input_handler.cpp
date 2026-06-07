#include "freertos/FreeRTOS.h"      // MUST be first
#include "freertos/queue.h"
#include "freertos/task.h"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "hardware/drivers/generic/button_driver.hpp"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <memory>
#include "os_code/middle_layer/input/hid_t.h"
#include "tusb.h"
#include "class/hid/hid.h"
#include "device/usbd_pvt.h"
static const char* TAG = "InputHandler";

extern QueueHandle_t ProcInputQueTarget;
DeviceManager gDeviceManager;
static const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(2))
};

// REMOVE these lines - they're causing redefinition:
// extern bool usb_hid_enabled=0; //declared in hpp
// static bool usb_hid_enabled = false;

// Define it ONCE (not extern, not static)
bool usb_hid_enabled = false;

// ====================== USB HID SUPPORT ======================


bool is_usb_hid_enabled(void) {
    return usb_hid_enabled;
}

void boot_hid_usb(bool enable)
{
    usb_hid_enabled = enable;
    if (enable) {
        ESP_LOGI(TAG, "USB HID Mode ACTIVATED (Keyboard + Mouse)");
        
        // don't think we need this at all because it's defined in tusb_config or tusb_descriptors.c and we need to change to be able to do it at runtime
       // tud_hid_set_report_descriptor(0, hid_report_descriptor, sizeof(hid_report_descriptor));
        tud_init(0);  // 0 = default RHPORT on ESP32-S3
    } else {
        ESP_LOGI(TAG, "USB HID Mode DISABLED");
    }
}

void hid_send_key(uint8_t keycode, bool pressed)
{
    if (!usb_hid_enabled || !tud_hid_ready()) return;

    uint8_t report[8] = {0};
    if (pressed) report[2] = keycode;

    tud_hid_report(0, report, sizeof(report));  // Report ID 0 = Keyboard
}

void hid_send_mouse_old(int8_t dx, int8_t dy, uint8_t buttons)
{
    if (!usb_hid_enabled || !tud_hid_ready()) return;

    uint8_t report[5] = {buttons, (uint8_t)dx, (uint8_t)dy, 0, 0};
    tud_hid_report(1, report, 5);   // Report ID 1 = Mouse
}

void hid_send_mouse(int8_t dx, int8_t dy, uint8_t buttons)
{
    if (!usb_hid_enabled) return;
    if (!tud_hid_ready()) return;

     // Parameters: report_id, buttons, x_offset, y_offset, vertical_wheel, horizontal_wheel
    tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, buttons, dx, dy, 0, 0);
}

// TinyUSB Callbacks
//extern "C" {
    uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id,
                                   hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
    {
        return 0;
    }

    void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
    {
        // Optional
    }
//}

// ===================================================================
// ButtonDevice
// ===================================================================
esp_err_t ButtonDevice::initialize(const button_config_t* cfg)
{
    if (btn_handle) {
        button_del(btn_handle);
        btn_handle = nullptr;
    }
    if (!cfg) return ESP_ERR_INVALID_ARG;

    button_config_t local = *cfg;
    local.on_press = pressCallback;
    local.on_release = nullptr;
    local.user_ctx = this;

    return button_new(&local, &btn_handle);
}

ButtonDevice::~ButtonDevice()
{
    if (btn_handle) button_del(btn_handle);
}

void ButtonDevice::update()
{
    if (btn_handle) button_poll(btn_handle);
}

void ButtonDevice::pressCallback(void* user_ctx, bool pressed)
{
    auto* self = static_cast<ButtonDevice*>(user_ctx);
    if (!self || !pressed) return;

    InputEvent ev{};
    ev.target = (HIDTarget)v_env.CurrentHIDTarget;
    ev.source_device_type = HIDInputDeviceType::Button;
    ev.key = self->props.press_key;
    ev.action = KeyAction::Tap;
    ev.timestamp = (uint32_t)(esp_timer_get_time() / 1000);

    if (ProcInputQueTarget) {
        xQueueSend(ProcInputQueTarget, &ev, pdMS_TO_TICKS(10));
    }
}

void ButtonDevice::interact(Device& other) { /* not used */ }

// ===================================================================
// KnobDevice
// ===================================================================
esp_err_t KnobDevice::initialize(const ky040_config_t* cfg)
{
    if (ky_handle) {
        ky040_del(ky_handle);
        ky_handle = nullptr;
    }
    if (!cfg) return ESP_ERR_INVALID_ARG;

    ky040_config_t local = *cfg;
    local.on_twist = twistCallback;
    local.user_ctx = this;

    esp_err_t ret = ky040_new(&local, &ky_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "KnobDevice initialized");
    } else {
        ESP_LOGE(TAG, "ky040_new failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

KnobDevice::~KnobDevice()
{
    if (ky_handle) ky040_del(ky_handle);
}

void KnobDevice::update()
{
    if (ky_handle) ky040_poll(ky_handle);
}

void KnobDevice::twistCallback(void* user_ctx, int delta)
{
    ESP_LOGI(TAG, "twistcallback");
    if (!user_ctx) return;

    auto* self = static_cast<KnobDevice*>(user_ctx);

    InputEvent ev{};
    ev.target = (HIDTarget)v_env.CurrentHIDTarget;
    ev.source_device_type = HIDInputDeviceType::Knob;
    ev.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
    ev.delta = delta;

    if (self->props.send_delta_instead_of_keys) {
        ev.action = KeyAction::PositionDelta;
    } else {
        ev.key    = (delta > 0) ? self->props.cw_key : self->props.ccw_key;
        ev.action = KeyAction::Tap;
    }

    if (ProcInputQueTarget) {
        xQueueSend(ProcInputQueTarget, &ev, pdMS_TO_TICKS(10));
    }
}

void KnobDevice::interact(Device& other) {}

// ===================================================================
// DeviceManager
// ===================================================================
void DeviceManager::addDevice(std::unique_ptr<Device> device)
{
    if (!device) {
        ESP_LOGE(TAG, "addDevice: received null device");
        return;
    }

    const char* name = device->getName();
    devices.push_back(std::move(device));

    ESP_LOGI(TAG, "Added device: %s (type %d)", name, (int)devices.back()->getType());
}

void DeviceManager::updateAll()
{
    for (auto& dev : devices) {
        dev->update();
    }
}

void DeviceManager::removeDevice(const char* name) {
    auto it = std::remove_if(devices.begin(), devices.end(),
        [name](const std::unique_ptr<Device>& dev) {
            return strcmp(dev->getName(), name) == 0;
        });
    devices.erase(it, devices.end());
}

void DeviceManager::listDevices() {
    ESP_LOGI("DeviceManager", "Connected devices: %d", devices.size());
    for (auto& dev : devices) {
        ESP_LOGI("DeviceManager", "  - %s (type %d)", dev->getName(), (int)dev->getType());
    }
}