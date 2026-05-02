#include "ipt_x.hpp"
#include "esp_log.h"
#include "os_code/core/rShell/s_hell.hpp"
#include "os_code/middle_layer/input/input_devs_agg.hpp"
#include "os_code/middle_layer/input/input_handler.hpp"

static const char* TAG = "InputTask";

// Queue for events from device callbacks (ISR or task context)
QueueHandle_t g_inputEventQueue = nullptr;

// Forward declarations
static void dispatch_event_to_focused_app(const InputEvent& ev);
static void handle_debug_output(const InputEvent& ev);

// -------------------------------------------------------------------
// Device callbacks (push events into queue)
// -------------------------------------------------------------------

static void twist_callback(void* user_ctx, int delta)
{
    auto* device = static_cast<KnobDevice*>(user_ctx);
    if (!device || !g_inputEventQueue) return;

    InputEvent ev{};
    ev.source_device_type = HIDInputDeviceType::Knob;
    ev.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
    ev.delta = delta;
    ev.target = CurrentHIDTarget;

    if (device->props.send_delta_instead_of_keys) {
        ev.action = KeyAction::PositionDelta;
    } else {
        ev.key = (delta > 0) ? device->props.cw_key : device->props.ccw_key;
        ev.action = KeyAction::Tap;
    }

    xQueueSend(g_inputEventQueue, &ev, pdMS_TO_TICKS(10));
}

static void button_callback(void* user_ctx, bool pressed)
{
    auto* device = static_cast<KnobDevice*>(user_ctx);
    if (!device || !g_inputEventQueue) return;

    if (pressed) {
        InputEvent ev{};
        ev.source_device_type = HIDInputDeviceType::Knob;
        ev.key = device->props.button_key;
        ev.action = KeyAction::Tap;
        ev.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        ev.target = CurrentHIDTarget;
        
        xQueueSend(g_inputEventQueue, &ev, pdMS_TO_TICKS(10));
    }
}

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
// Device initialization helper
// -------------------------------------------------------------------

static void init_input_devices()
{
    /*
    // Configure knob (KY-040)
    ky040_config_t knob_config = {
        .clk_pin = (gpio_num_t)36,      // adjust to your pins
        .dt_pin = (gpio_num_t)35,
        .sw_pin = (gpio_num_t)34,
        .on_twist = twist_callback,
        .on_button = button_callback,
        .user_ctx = nullptr,  // will be set in the device
    };

    auto knob = std::make_unique<KnobDevice>();
    knob->props.cw_key = KEY_DOWN;
    knob->props.ccw_key = KEY_UP;
    knob->props.button_key = KEY_ENTER;
    knob->props.send_delta_instead_of_keys = false;
    
    // Pass the device pointer as user_ctx
    knob_config.user_ctx = knob.get();
    knob->initialize(&knob_config);
    
    gDeviceManager.addDevice(std::move(knob));
    ESP_LOGI(TAG, "Input devices initialized");*/
}

// -------------------------------------------------------------------
// Main input task (does everything)
// -------------------------------------------------------------------

static void input_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Input task started");
    
    // Create queue for device callbacks
    g_inputEventQueue = xQueueCreate(32, sizeof(InputEvent));
    if (!g_inputEventQueue) {
        ESP_LOGE(TAG, "Failed to create input event queue");
        vTaskDelete(nullptr);
        return;
    }
    
    // Initialize all hardware devices
    init_input_devices();
    
    InputEvent ev;
    ev.target=HIDTarget::toTaskAndDebug; //default that motherfucker
    TickType_t last_wake = xTaskGetTickCount();
    
    while (true)
    {
        // 1. Poll all devices (they push events into the queue)
        gDeviceManager.updateAll();
        
        // 2. Process all pending events from the queue
        while (xQueueReceive(g_inputEventQueue, &ev, 0) == pdTRUE)
        {
            switch (ev.target)
            {
                case HIDTarget::nothing:
                    // ignore
                    break;
                    
                case HIDTarget::debug_log:
                    handle_debug_output(ev);
                    break;
                    
                case HIDTarget::toTask:
                dispatch_event_to_focused_app(ev);
                break;

                case HIDTarget::toTask_and_usbHid:
                    dispatch_event_to_focused_app(ev);
                    //should probably put that in here ig
                    break;
                    
                case HIDTarget::everything:
                    ESP_LOGW(TAG, "HIDTarget::everything - not supported");
                    break;

                case HIDTarget::toTaskAndDebug:
                dispatch_event_to_focused_app(ev);
                handle_debug_output(ev);
                break;    
                    
                default:
                    // fallback: send to app manager
                    dispatch_event_to_focused_app(ev);
                    break;
            }
        }
        
        // 3. Wait for next cycle (10ms = 100Hz polling)
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
    }
}

// -------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------

void startInputTask()
{
    xTaskCreate(input_task, "input_task", 4096, nullptr, 4, nullptr);
}