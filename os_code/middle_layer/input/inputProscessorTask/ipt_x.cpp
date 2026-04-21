#include "os_code/middle_layer/input/input_handler.hpp"

#include "esp_log.h"
#include "os_code/core/rShell/s_hell.hpp"
#include "os_code/middle_layer/input/input_devs_agg.hpp"
#include "os_code/middle_layer/input/input_handler.hpp"

static const char* TAG = "InputHandler";



void route_input_to_app_manager(const InputEvent& ev) {
    appManager::instance().route_input_to_focused(ev);
}

static void input_consumer_task(void* pvParameters)
{
    InputEvent ev;
    TickType_t last_wake_time = xTaskGetTickCount();

    ESP_LOGI(TAG, "Input consumer task started");

    while (true)
    {
        // Poll all devices for new events
        gDeviceManager.updateAll();

        // Drain queue
        while (xQueueReceive(ProcInputQueTarget, &ev, 0) == pdTRUE)
        {
            switch (ev.target)
            {
                case HIDTarget::nothing:
                    // Do nothing
                    break;

                case HIDTarget::debug_log:
                    // Just print to serial / ESP log
                    switch (ev.key) {
                        case KEY_UP:    ESP_LOGI(TAG, "↑ UP (left knob)"); break; case KEY_DOWN:  ESP_LOGI(TAG, "↓ DOWN (left knob)"); break;
                        case KEY_LEFT:  ESP_LOGI(TAG, "← LEFT (right knob)"); break; case KEY_RIGHT: ESP_LOGI(TAG, "→ RIGHT (right knob)"); break;
                        case KEY_ENTER: ESP_LOGI(TAG, "ENTER"); break; case KEY_BACK:  ESP_LOGI(TAG, "BACK"); break;
                        default:
                            if (ev.action == KeyAction::PositionDelta) {
                                ESP_LOGI(TAG, "Knob delta: %+d", ev.delta);
                            }
                            break;
                    }
                    break;

                    case HIDTarget::toTask:
                    route_input_to_app_manager(ev);
                    break;
                
                case HIDTarget::toTask_and_usbHid:
                    route_input_to_app_manager(ev);
                    // sendUsbHidEvent(ev);  // if needed
                    break;

                case HIDTarget::wireless_hid:
                    // Forward to wireless HID (BLE, etc)
                   // sendWirelessHidEvent(ev);
                    break;

               

                case HIDTarget::everything:
                    // Optional: fallback / special debug
                    ESP_LOGW(TAG, "HIDTarget::everything received, what the fuck is wrong with you");
                    //handleTaskInput(ev);
                    //sendUsbHidEvent(ev);
                    //sendWirelessHidEvent(ev);
                    break;

                default:
                    // Fallback in case target was uninitialized
                  //  handleTaskInput(ev);
                    break;
            }
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(10));  // 100 Hz
    }
}





void startInputHandlerTask() {
    xTaskCreate(input_consumer_task, "input_consumer", 16384, nullptr, 4, nullptr);
}