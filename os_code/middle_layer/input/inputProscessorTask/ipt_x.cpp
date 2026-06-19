#include "ipt_x.hpp"
#include "esp_log.h"
#include "os_code/core/rShell/s_hell.hpp"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "tusb.h"
#include "os_code/middle_layer/input/hid_t.h"
#include "esp_task_wdt.h" 
#include "os_code/applications/toolkit/RemoteKeyboard/keymap.hpp"
static const char* TAG = "InputTask";

// External queue
extern QueueHandle_t ProcInputQueTarget;

// extern ref specific
extern bool usb_hid_enabled;

// Forward declarations




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




// -------------------------------------------------------------------
// Main Input Task
// -------------------------------------------------------------------
static void input_task(void* pvParameters) 
{
    ESP_LOGI(TAG, "================Input task started=============");

    // Wait for the queue
    while (!ProcInputQueTarget) {
        ESP_LOGW(TAG, "Waiting for ProcInputQueTarget...");
        vTaskDelay(pdMS_TO_TICKS(80));
        esp_task_wdt_reset();
    }

    register_input_task_for_notifications(xTaskGetCurrentTaskHandle());

    InputEvent ev;
    const TickType_t button_poll_interval = pdMS_TO_TICKS(33);

    // Subscribe to watchdog
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    
        
    
    while (true) {
        // === ALWAYS reset at the start of the loop ===
        esp_task_wdt_reset();
        
        uint32_t notified = ulTaskNotifyTake(pdTRUE, button_poll_interval);

        if (notified) {
            gDeviceManager.updateEncoder();
        }
        
        gDeviceManager.updateButtons();
        gDeviceManager.updateEncoder(); //we do this for now to save time in developement, we'll later need to have the device manager tick every device present, prioritizing high frequency devices. 
        // Process queue (may be empty)
        bool processed_any = false;
        while (xQueueReceive(ProcInputQueTarget, &ev, 0) == pdTRUE) {
            processed_any = true;
            ESP_LOGI(TAG, "Event key=0x%04X target=%d", ev.key, (int)ev.target);


           // task = xTaskGetCurrentTaskHandle();    high_water = uxTaskGetStackHighWaterMark(task);
           // ESP_LOGI("STACK", "%s: High water mark = %u words (%u bytes free)", task_name, high_water, high_water * 4);


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

        // Extra safety: reset after potentially heavy queue processing
        if (processed_any) {
            esp_task_wdt_reset();
        }
        
        // Optional: very light yield if nothing happened
        // (helps when the task is spinning too fast)
         vTaskDelay(pdMS_TO_TICKS(1)); // only if needed
    }
}

// -------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------
void startInputTask()
{
    ESP_LOGI(TAG, "Creating input task...");
    BaseType_t result = xTaskCreatePinnedToCore(input_task, "input_task", 8192, nullptr, 1, nullptr, 0);

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create input task!");
    } else {
        ESP_LOGI(TAG, "Input task started successfully");
    }
}