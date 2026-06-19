//implementation for rubberducky helper functions// os_code/applications/toolkit/RemoteKeyboard/tk_rd.cpp
#include "tk_rd.hpp"
#include "esp_log.h"
#include "os_code/middle_layer/input/input_handler.hpp"


#include "tk_rd.hpp"
#include "esp_log.h"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/middle_layer/input/keymap.hpp"

static const char* TAG = "RubberDuckyApp";

RubberDuckyApp::RubberDuckyApp() {
    m_parser = std::make_unique<RubberDucky::DuckyParser>();
    
    m_parser->setOnKeySend([](uint8_t keycode, uint8_t modifiers, bool pressed) {
        hid_send_key_with_modifiers(keycode, modifiers, pressed);
    });
    
    m_parser->setOnDelay([](uint32_t ms) {
        ESP_LOGD(TAG, "Delay %lu ms", ms);
    });
}


RubberDuckyApp::RubberDuckyApp() {
    m_parser = std::make_unique<RubberDucky::DuckyParser>();
    
    // Connect USB HID callbacks
    m_parser->setOnKeySend([](uint8_t keycode, bool pressed) {
        hid_send_key(keycode, pressed);
    });
    
    m_parser->setOnDelay([](uint32_t ms) {
        // Just log for now, vTaskDelay is handled inside parser
        ESP_LOGD(TAG, "Delay %lu ms", ms);
    });
}

RubberDuckyApp::~RubberDuckyApp() = default;

void RubberDuckyApp::on_attach() {
    ESP_LOGI(TAG, "RubberDuckyApp attached");
    // Scan /scripts/ directory for .duck files
    // TODO: Implement FATFS directory scan
    m_scriptFiles = {"payload1.duck", "payload2.duck"}; // Placeholder
}

void RubberDuckyApp::on_detach() {
    if (m_state == State::EXECUTING) {
        m_parser->stop();
    }
    ESP_LOGI(TAG, "RubberDuckyApp detached");
}

void RubberDuckyApp::on_tick() {
    if (m_state == State::EXECUTING) {
        // Could update progress here
    }
}

void RubberDuckyApp::on_draw() {
    // Draw UI based on state
}

void RubberDuckyApp::on_input(const InputEvent& ev) {
    switch (m_state) {
        case State::SELECTING_FILE:
            if (ev.key == KEY_UP) m_selectedIndex--;
            if (ev.key == KEY_DOWN) m_selectedIndex++;
            if (ev.key == KEY_ENTER && m_selectedIndex < (int)m_scriptFiles.size()) {
                // Load and execute selected script
                if (m_parser->loadFile(m_scriptFiles[m_selectedIndex].c_str())) {
                    m_state = State::EXECUTING;
                    // Start execution in separate task
                    // ...
                }
            }
            break;
            
        case State::EXECUTING:
            if (ev.key == KEY_BACK) {
                m_parser->stop();
                m_state = State::IDLE;
            }
            break;
            
        default:
            break;
    }
}

// In RubberDuckyApp::RubberDuckyApp()
m_parser->setOnKeySend([](uint8_t keycode, uint8_t modifiers, bool pressed) {
    hid_send_key_with_modifiers(keycode, modifiers, pressed);
});

Recommendation: Use the central get_hid_keycode() everywhere now.
Would you like me to:

Finish the full updated kb_rd.cpp with better parsing?
Add SD card file listing for .duck files?
Add a background execution task so the UI doesn't freeze during long scripts?

Let me know what you want next and I'll deliver the complete updated files.