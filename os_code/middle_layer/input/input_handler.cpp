#include "freertos/FreeRTOS.h"      // MUST be first
#include "freertos/queue.h"
#include "freertos/task.h"

#include "os_code/middle_layer/input/input_handler.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <memory>

static const char* TAG = "InputHandler";

extern QueueHandle_t ProcInputQueTarget;   // defined in main
HIDTarget CurrentHIDTarget=HIDTarget::debug_log; //default behavior, not the target task. we have an input handler task for a reason, so this is global so every other task can see what's happening with that
DeviceManager gDeviceManager;   // Global instance of the input handler object
// ===================================================================
// KnobDevice Implementation
// ===================================================================
//yeah we'll just assume the ky040 is what matters here
esp_err_t KnobDevice::initialize(const ky040_config_t* cfg)
{
    if (ky_handle) {
        ky040_del(ky_handle);
        ky_handle = nullptr;
    }
    if (!cfg) return ESP_ERR_INVALID_ARG;

    ky040_config_t local = *cfg;
    local.on_twist = twistCallback;
    local.on_button = buttonCallback; //for buttons because yeah whatever
    local.user_ctx = this;

    esp_err_t ret = ky040_new(&local, &ky_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "KnobDevice initialized: CW=0x%04X, CCW=0x%04X", 
                 props.cw_key, props.ccw_key);
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
    if (ky_handle) {
        ky040_poll(ky_handle);

        // Simple button polling (edge detection)
        static bool last_btn = false;
        bool now = ky040_is_button_pressed(ky_handle);
        if (now && !last_btn) {
            // Button just pressed
            InputEvent ev{};
            ev.source_device_type = HIDInputDeviceType::Knob;
            ev.key = props.button_key;
            ev.action = KeyAction::Tap;
            ev.timestamp = (uint32_t)(esp_timer_get_time() / 1000);

            if (ProcInputQueTarget) {
                xQueueSend(ProcInputQueTarget, &ev, pdMS_TO_TICKS(10));
            }
        }
        last_btn = now;
    }
}

void KnobDevice::twistCallback(void* user_ctx, int delta)
{
    if (!user_ctx) return;
    auto* self = static_cast<KnobDevice*>(user_ctx);

    InputEvent ev{};
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

 void KnobDevice::buttonCallback(void* user_ctx, bool pressed) {
    if (!user_ctx) return;
    auto* self = static_cast<KnobDevice*>(user_ctx);

    InputEvent ev{};
    ev.source_device_type = HIDInputDeviceType::Knob;
    ev.timestamp = (uint32_t)(esp_timer_get_time() / 1000);

    if (pressed) {
        ev.key = self->props.button_key;   // e.g., KEY_ENTER or KEY_BACK
        ev.action = KeyAction::Tap;

        if (ProcInputQueTarget) {
            xQueueSend(ProcInputQueTarget, &ev, pdMS_TO_TICKS(10));
        }
    }
}

void KnobDevice::interact(Device& other) {
    // TODO: future - knob can control another device (e.g. scroll a list)
}

// ===================================================================
// DeviceManager
// ===================================================================
void DeviceManager::addDevice(std::unique_ptr<Device> device)
{
    if (!device) {
        ESP_LOGE(TAG, "addDevice: received null device");
        return;
    }

    const char* name = device->getName();           // safe copy
    devices.push_back(std::move(device));

    ESP_LOGI(TAG, "Added device: %s (type %d)", name, (int)devices.back()->getType());
}

void DeviceManager::updateAll()
{
    for (auto& dev : devices) {
        dev->update();
    }
}