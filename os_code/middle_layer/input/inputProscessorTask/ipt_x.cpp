#include "ipt_x.hpp"
#include "esp_log.h"
#include "os_code/core/rShell/s_hell.hpp"
#include "os_code/middle_layer/input/input_handler.hpp"

static const char* TAG = "InputTask";

// External queue from input_handler
extern QueueHandle_t ProcInputQueTarget;

// Forward declarations
static void dispatch_event_to_focused_app(const InputEvent& ev);
static void handle_debug_output(const InputEvent& ev);

// -------------------------------------------------------------------
// Event dispatching
// -------------------------------------------------------------------

static void dispatch_event_to_focused_app(const InputEvent& ev)
{
    auto& mgr = appManager::instance();
    mgr.route_input_to_focused(ev);
}

static void handle_debug_output(const InputEvent& ev)
{
    switch (ev.key) {
        case KEY_UP:    ESP_LOGI(TAG, "↑ UP"); break;
        case KEY_DOWN:  ESP_LOGI(TAG, "↓ DOWN"); break;
        case KEY_LEFT:  ESP_LOGI(TAG, "← LEFT"); break;
        case KEY_RIGHT: ESP_LOGI(TAG, "→ RIGHT"); break;
        case KEY_ENTER: ESP_LOGI(TAG, "ENTER"); break;
        case KEY_BACK:  ESP_LOGI(TAG, "BACK"); break;
        default:
            if (ev.action == KeyAction::PositionDelta) {
                ESP_LOGI(TAG, "Knob delta: %+d", ev.delta);
            }
            break;
    }
}

// -------------------------------------------------------------------
// Main input task - JUST processes events from the queue
// -------------------------------------------------------------------

static void input_task(void* pvParameters)
{
    ESP_LOGI(TAG, "================Input task started - waiting for events=============");
    
    // Make sure the queue exists (created in main)
    while (!ProcInputQueTarget) {
        ESP_LOGW(TAG, "Waiting for ProcInputQueTarget...");
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    
    InputEvent ev;
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t poll_interval = pdMS_TO_TICKS(10);  // 100Hz polling
    
    while (true)
    {
        // ✅ POLL ALL DEVICES - THIS IS CRITICAL!
        gDeviceManager.updateAll();
        
        // Check for events (non-blocking, since we're polling)
        while (xQueueReceive(ProcInputQueTarget, &ev, 0) == pdTRUE)
        {
            ESP_LOGI(TAG, " Received input event! key=0x%04X", ev.key);
            
            switch (ev.target)
            {
                case HIDTarget::debug_log:
                    handle_debug_output(ev);
                    break;
                    
                case HIDTarget::toTask:
                case HIDTarget::toTaskAndDebug:
                    dispatch_event_to_focused_app(ev);
                    handle_debug_output(ev);
                    break;
                    
                default:
                    dispatch_event_to_focused_app(ev);
                    break;
            }
        }
        //ESP_LOGI(TAG, "bitch bitch bitch bitch");//ok so it runs but it dont detect input?!
        // Maintain 100Hz polling rate
        vTaskDelayUntil(&last_wake, poll_interval);
    }
}

// -------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------

void startInputTask()
{
    ESP_LOGI(TAG, "Input task creating");
    BaseType_t result = xTaskCreatePinnedToCore(input_task, "input_task", 6144, nullptr, 1, nullptr, 0);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "FAILED to create input task! Error: %d", result);
    } else {
        ESP_LOGI(TAG, "Input task created successfully");
    }
}