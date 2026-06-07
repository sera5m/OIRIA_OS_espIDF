#include "ipt_x.hpp"
#include "esp_log.h"
#include "os_code/core/rShell/s_hell.hpp"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "tusb.h"
//#include "class/hid/hid.h"

static const char* TAG = "InputTask";

// External queue
extern QueueHandle_t ProcInputQueTarget;

// extern ref specific
extern bool usb_hid_enabled;

// Forward declarations
static void dispatch_event_to_focused_app(const InputEvent& ev);
static void handle_debug_output(const InputEvent& ev);



// -------------------------------------------------------------------
// Event Handlers
// -------------------------------------------------------------------
static void dispatch_event_to_focused_app(const InputEvent& ev)
{
    auto& mgr = appManager::instance();
    mgr.route_input_to_focused(ev);
}

static void handle_debug_output(const InputEvent& ev)
{
    switch (ev.key) {
        case KEY_UP:    ESP_LOGI(TAG, " UP"); break;
        case KEY_DOWN:  ESP_LOGI(TAG, " DOWN"); break;
        case KEY_LEFT:  ESP_LOGI(TAG, "LEFT"); break;
        case KEY_RIGHT: ESP_LOGI(TAG, " RIGHT"); break;
        case KEY_ENTER: ESP_LOGI(TAG, "ENTER"); break;
        case KEY_BACK:  ESP_LOGI(TAG, "BACK"); break;
        default:
            if (ev.action == KeyAction::PositionDelta) {
                ESP_LOGI(TAG, "Knob delta: %+d", ev.delta);
            }
            break;
    }
}
static void handle_usb_hid(const InputEvent& ev)
{
    if (!usb_hid_enabled) return;   // Note: this is declared in input_handler.cpp

    switch (ev.key) {
        case KEY_UP:    hid_send_key(HID_KEY_ARROW_UP,    ev.action == KeyAction::Tap); break;
        case KEY_DOWN:  hid_send_key(HID_KEY_ARROW_DOWN,  ev.action == KeyAction::Tap); break;
        case KEY_LEFT:  hid_send_key(HID_KEY_ARROW_LEFT,  ev.action == KeyAction::Tap); break;
        case KEY_RIGHT: hid_send_key(HID_KEY_ARROW_RIGHT, ev.action == KeyAction::Tap); break;
        case KEY_ENTER: hid_send_key(HID_KEY_ENTER,       ev.action == KeyAction::Tap); break;
        case KEY_BACK:  hid_send_key(HID_KEY_END,         ev.action == KeyAction::Tap); break;

        default:
            if (ev.action == KeyAction::PositionDelta) {
                // Pass buttons=0 for scroll without button clicks
                hid_send_mouse(0, ev.delta * 3, 0);  // Added the 3rd argument
            }
            break;
    }
}



void RouteInput_HidTarget(InputEvent i_ev){
    switch (i_ev.key) {
        case KEY_UP:    hid_send_key(HID_KEY_ARROW_UP, i_ev.action == KeyAction::Tap); break;
        case KEY_DOWN:  hid_send_key(HID_KEY_ARROW_DOWN, i_ev.action == KeyAction::Tap); break;
        case KEY_LEFT:  hid_send_key(HID_KEY_ARROW_LEFT, i_ev.action == KeyAction::Tap); break;
        case KEY_RIGHT: hid_send_key(HID_KEY_ARROW_RIGHT, i_ev.action == KeyAction::Tap); break;
        case KEY_ENTER: hid_send_key(HID_KEY_ENTER, i_ev.action == KeyAction::Tap); break;
        case KEY_BACK:  hid_send_key(HID_KEY_END, i_ev.action == KeyAction::Tap); break;
        default:
            if (i_ev.action == KeyAction::PositionDelta) {
                hid_send_mouse(0, i_ev.delta * 3, 0);
            }
            break;
    }
}



// -------------------------------------------------------------------
// Main Input Task
// -------------------------------------------------------------------
static void input_task(void* pvParameters) {
    ESP_LOGI(TAG, "================Input task started=============");

    // Wait for the queue to be created by main
    while (!ProcInputQueTarget) {
        ESP_LOGW(TAG, "Waiting for ProcInputQueTarget...");
        vTaskDelay(pdMS_TO_TICKS(80));
    }

    InputEvent ev;
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t poll_interval = pdMS_TO_TICKS(8);  // 8ms = 125Hz

    while (true) {
        // 1. Poll all hardware devices
        gDeviceManager.updateAll();

        // 2. Process all pending input events
        while (xQueueReceive(ProcInputQueTarget, &ev, 0) == pdTRUE) {
            ESP_LOGI(TAG, "Event key=0x%04X target=%d", ev.key, (int)ev.target);

            switch (ev.target) {
                case HIDTarget::actAsUsbHID:
                    if (usb_hid_enabled) RouteInput_HidTarget(ev);
                    break;
                case HIDTarget::toTask_and_usbHid:
                    if (usb_hid_enabled) RouteInput_HidTarget(ev);
                    dispatch_event_to_focused_app(ev);
                    break;
                case HIDTarget::toTask:
                    dispatch_event_to_focused_app(ev);
                    break;
                case HIDTarget::toTaskAndDebug:
                    dispatch_event_to_focused_app(ev);
                    handle_debug_output(ev);
                    break;
                case HIDTarget::debug_log:
                    handle_debug_output(ev);
                    break;
                default:
                    dispatch_event_to_focused_app(ev);
                    break;
            }
        }

        // 3. Smart delay: if we're late, skip waiting
        TickType_t now = xTaskGetTickCount();
        TickType_t time_since_last_wake = now - last_wake;
        
        if (time_since_last_wake >= poll_interval) {
            // We're behind schedule - don't delay, just update last_wake
            last_wake = now;
            // Continue to next iteration immediately
        } else {
            // Wait until next scheduled wake time
            vTaskDelayUntil(&last_wake, poll_interval);
        }
    }
}

// -------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------
void startInputTask()
{
    ESP_LOGI(TAG, "Creating input task...");
    BaseType_t result = xTaskCreatePinnedToCore(input_task, "input_task", 6144, nullptr, 1, nullptr, 0);

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create input task!");
    } else {
        ESP_LOGI(TAG, "Input task started successfully");
    }
}