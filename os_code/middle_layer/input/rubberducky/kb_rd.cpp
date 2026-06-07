// os_code/middle_layer/input/rubberducky/kb_rd.cpp
#include "kb_rd.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tusb.h"
#include <cctype>
#include <sstream>

static const char* TAG = "DuckyParser";

namespace RubberDucky {

// Key mapping table (simplified - expand as needed)
struct KeyMap {
    const char* name;
    uint8_t hid_code;
};

static const KeyMap keymap[] = {
    {"ENTER", HID_KEY_ENTER},
    {"CTRL", HID_KEY_CONTROL},
    {"CONTROL", HID_KEY_CONTROL},
    {"ALT", HID_KEY_ALT},
    {"SHIFT", HID_KEY_SHIFT},
    {"GUI", HID_KEY_GUI},
    {"WINDOWS", HID_KEY_GUI},
    {"TAB", HID_KEY_TAB},
    {"ESC", HID_KEY_ESCAPE},
    {"SPACE", HID_KEY_SPACE},
    {"BACKSPACE", HID_KEY_BACKSPACE},
    {"DELETE", HID_KEY_DELETE},
    {"UP", HID_KEY_ARROW_UP},
    {"DOWN", HID_KEY_ARROW_DOWN},
    {"LEFT", HID_KEY_ARROW_LEFT},
    {"RIGHT", HID_KEY_ARROW_RIGHT},
    {"HOME", HID_KEY_HOME},
    {"END", HID_KEY_END},
    {"PAGEUP", HID_KEY_PAGE_UP},
    {"PAGEDOWN", HID_KEY_PAGE_DOWN},
    {"CAPSLOCK", HID_KEY_CAPS_LOCK},
    {"NUMLOCK", HID_KEY_NUM_LOCK},
    {"SCROLLLOCK", HID_KEY_SCROLL_LOCK},
    {"PRINTSCREEN", HID_KEY_PRINT_SCREEN},
    {"PAUSE", HID_KEY_PAUSE},
    {"MENU", HID_KEY_APP},
    {"F1", HID_KEY_F1}, {"F2", HID_KEY_F2}, {"F3", HID_KEY_F3},
    {"F4", HID_KEY_F4}, {"F5", HID_KEY_F5}, {"F6", HID_KEY_F6},
    {"F7", HID_KEY_F7}, {"F8", HID_KEY_F8}, {"F9", HID_KEY_F9},
    {"F10", HID_KEY_F10}, {"F11", HID_KEY_F11}, {"F12", HID_KEY_F12},
    {nullptr, 0}
};

uint8_t duckyKeyToHID(const std::string& keyname) {
    for (int i = 0; keymap[i].name; i++) {
        if (strcasecmp(keyname.c_str(), keymap[i].name) == 0) {
            return keymap[i].hid_code;
        }
    }
    // Single character a-z / A-Z / 0-9
    if (keyname.length() == 1) {
        char c = keyname[0];
        if (c >= 'a' && c <= 'z') return HID_KEY_A + (c - 'a');
        if (c >= 'A' && c <= 'Z') return HID_KEY_A + (c - 'A');
        if (c >= '0' && c <= '9') return HID_KEY_1 + (c - '0');
    }
    ESP_LOGW(TAG, "Unknown key: %s", keyname.c_str());
    return 0;
}

bool DuckyParser::loadFile(const char* filename) {
    // TODO: Implement FATFS file reading
    // FILE* f = fopen(filename, "r");
    // Read entire file into string, call parse()
    return false;
}

bool DuckyParser::parse(const std::string& content) {
    m_commands.clear();
    std::istringstream stream(content);
    std::string line;
    int line_num = 0;
    
    while (std::getline(stream, line)) {
        line_num++;
        
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        
        // Skip comments
        if (line[0] == '#') continue;
        
        if (!parseLine(line)) {
            ESP_LOGE(TAG, "Parse error line %d: %s", line_num, line.c_str());
            return false;
        }
    }
    
    ESP_LOGI(TAG, "Parsed %zu commands", m_commands.size());
    return true;
}

bool DuckyParser::parseLine(const std::string& line) {
    std::string upper = line;
    for (auto& c : upper) c = toupper(c);
    
    DuckyCommand cmd;
    cmd.repeat_count = 1;
    
    // REM command - ignore
    if (upper.find("REM") == 0) {
        return true;
    }
    
    // STRING / STRINGLN
    if (upper.find("STRINGLN") == 0) {
        cmd.cmd = DuckCmd::STRINGLN;
        cmd.param = line.substr(9); // after "STRINGLN "
        // Trim leading space
        if (!cmd.param.empty() && cmd.param[0] == ' ') cmd.param.erase(0, 1);
        m_commands.push_back(cmd);
        return true;
    }
    
    if (upper.find("STRING") == 0) {
        cmd.cmd = DuckCmd::STRING;
        cmd.param = line.substr(7);
        if (!cmd.param.empty() && cmd.param[0] == ' ') cmd.param.erase(0, 1);
        m_commands.push_back(cmd);
        return true;
    }
    
    // DELAY
    if (upper.find("DELAY") == 0) {
        cmd.cmd = DuckCmd::DELAY;
        cmd.param = line.substr(6);
        m_commands.push_back(cmd);
        return true;
    }
    
    // DEFAULT_DELAY
    if (upper.find("DEFAULT_DELAY") == 0) {
        cmd.cmd = DuckCmd::DEFAULT_DELAY;
        cmd.param = line.substr(14);
        m_commands.push_back(cmd);
        return true;
    }
    
    // REPEAT
    if (upper.find("REPEAT") == 0) {
        cmd.cmd = DuckCmd::REPEAT;
        cmd.param = line.substr(7);
        m_commands.push_back(cmd);
        return true;
    }
    
    // Single key or modifier combo
    cmd.cmd = DuckCmd::STRING; // Default to typing keys
    cmd.param = line;
    m_commands.push_back(cmd);
    
    return true;
}

void DuckyParser::execute() {
    if (m_commands.empty()) {
        ESP_LOGE(TAG, "No commands to execute");
        return;
    }
    
    m_running = true;
    
    for (size_t i = 0; i < m_commands.size() && m_running; i++) {
        executeCommand(m_commands[i]);
        
        // Default delay between commands
        if (m_defaultDelay > 0 && m_commands[i].cmd != DuckCmd::DELAY) {
            if (m_onDelay) m_onDelay(m_defaultDelay);
            vTaskDelay(pdMS_TO_TICKS(m_defaultDelay));
        }
    }
    
    // Release all keys at the end
    if (m_onKeySend) {
        // Send empty report to release all keys
        for (int i = 0; i < 6; i++) {
            m_onKeySend(0, false);
        }
    }
    
    m_running = false;
    ESP_LOGI(TAG, "Script execution complete");
}

void DuckyParser::executeCommand(const DuckyCommand& cmd) {
    switch (cmd.cmd) {
        case DuckCmd::STRING:
            for (char c : cmd.param) {
                if (m_onKeySend) {
                    bool needs_shift = (c >= 'A' && c <= 'Z');
                    uint8_t keycode;
                    
                    if (c >= 'a' && c <= 'z') keycode = HID_KEY_A + (c - 'a');
                    else if (c >= 'A' && c <= 'Z') keycode = HID_KEY_A + (c - 'A');
                    else if (c >= '0' && c <= '9') keycode = HID_KEY_1 + (c - '0');
                    else {
                        // Handle symbols - simplified
                        keycode = 0;
                    }
                    
                    if (needs_shift && m_onKeySend) m_onKeySend(HID_KEY_SHIFT, true);
                    if (keycode) m_onKeySend(keycode, true);
                    vTaskDelay(pdMS_TO_TICKS(5));
                    if (keycode) m_onKeySend(keycode, false);
                    if (needs_shift && m_onKeySend) m_onKeySend(HID_KEY_SHIFT, false);
                    
                    if (m_defaultCharDelay > 0) vTaskDelay(pdMS_TO_TICKS(m_defaultCharDelay));
                }
            }
            break;
            
        case DuckCmd::STRINGLN:
            executeCommand({DuckCmd::STRING, cmd.param, 1});
            executeCommand({DuckCmd::STRING, "\n", 1});
            break;
            
        case DuckCmd::DELAY: {
            uint32_t ms = atoi(cmd.param.c_str());
            if (m_onDelay) m_onDelay(ms);
            vTaskDelay(pdMS_TO_TICKS(ms));
            break;
        }
        
        case DuckCmd::DEFAULT_DELAY:
            m_defaultDelay = atoi(cmd.param.c_str());
            ESP_LOGI(TAG, "Default delay set to %lu ms", m_defaultDelay);
            break;
            
        case DuckCmd::DEFAULTCHARDELAY:
            m_defaultCharDelay = atoi(cmd.param.c_str());
            break;
            
        default:
            // Single keypress
            uint8_t keycode = duckyKeyToHID(cmd.param);
            if (keycode && m_onKeySend) {
                m_onKeySend(keycode, true);
                vTaskDelay(pdMS_TO_TICKS(10));
                m_onKeySend(keycode, false);
            }
            break;
    }
    
    m_lastCommand = cmd;
}

void DuckyParser::setOnKeySend(std::function<void(uint8_t, bool)> callback) {
    m_onKeySend = callback;
}

void DuckyParser::setOnDelay(std::function<void(uint32_t)> callback) {
    m_onDelay = callback;
}

} // namespace RubberDucky