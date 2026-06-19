#include "kb_rd.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sstream>
#include <cctype>
#include <cstdio>

#include "os_code/middle_layer/input/hid.h"











namespace RubberDucky {

static const char* TAG = "DuckyParser";

DuckyParser::DuckyParser() = default;
DuckyParser::~DuckyParser() = default;

bool DuckyParser::loadFile(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s", filename);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    std::string content(size, '\0');
    fread(&content[0], 1, size, f);
    fclose(f);

    return parse(content);
}

bool DuckyParser::parse(const std::string& content) {
    m_commands.clear();
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        parseLine(line);
    }
    ESP_LOGI(TAG, "Parsed %zu DuckyScript commands", m_commands.size());
    return true;
}

bool DuckyParser::parseLine(const std::string& raw_line) {
    std::string line = raw_line;
    // Trim
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) return true;
    line = line.substr(start);

    if (line.empty() || line[0] == '#') return true;

    std::string upper = line;
    for (char& c : upper) c = toupper(c);

    DuckyCommand cmd;

    if (upper.rfind("REM", 0) == 0) return true;

    if (upper.rfind("STRINGLN", 0) == 0) {
        cmd.cmd = DuckCmd::STRINGLN;
        cmd.param = line.substr(9);
    } else if (upper.rfind("STRING", 0) == 0) {
        cmd.cmd = DuckCmd::STRING;
        cmd.param = line.substr(7);
    } else if (upper.rfind("DELAY", 0) == 0) {
        cmd.cmd = DuckCmd::DELAY;
        cmd.param = line.substr(6);
    } else if (upper.rfind("DEFAULT_DELAY", 0) == 0) {
        cmd.cmd = DuckCmd::DEFAULT_DELAY;
        cmd.param = line.substr(14);
    } else if (upper.rfind("DEFAULTCHARDELAY", 0) == 0) {
        cmd.cmd = DuckCmd::DEFAULTCHARDELAY;
        cmd.param = line.substr(17);
    } else {
        // Treat as single key command
        cmd.cmd = DuckCmd::STRING;
        cmd.param = line;
    }

    m_commands.push_back(std::move(cmd));
    return true;
}

void DuckyParser::execute() {
    if (m_commands.empty()) return;

    m_running = true;

    for (const auto& cmd : m_commands) {
        if (!m_running) break;
        executeCommand(cmd);
    }

    // Release all keys
    if (m_onKeySend) m_onKeySend(0, 0, false);

    m_running = false;
    ESP_LOGI(TAG, "DuckyScript execution finished");
}

void DuckyParser::executeCommand(const DuckyCommand& cmd) {
    switch (cmd.cmd) {
        case DuckCmd::STRING:
        case DuckCmd::STRINGLN: {
            for (char c : cmd.param) {
                uint8_t mod = 0;
                uint8_t key = get_hid_keycode((uint16_t)c, &mod);

                if (key && m_onKeySend) {
                    m_onKeySend(key, mod, true);
                    vTaskDelay(pdMS_TO_TICKS(m_defaultCharDelay));
                    m_onKeySend(key, mod, false);
                }
            }
            if (cmd.cmd == DuckCmd::STRINGLN) {
                uint8_t enter = HID_KEY_ENTER;
                if (m_onKeySend) {
                    m_onKeySend(enter, 0, true);
                    vTaskDelay(pdMS_TO_TICKS(10));
                    m_onKeySend(enter, 0, false);
                }
            }
            break;
        }

        case DuckCmd::DELAY: {
            uint32_t ms = atoi(cmd.param.c_str());
            if (m_onDelay) m_onDelay(ms);
            vTaskDelay(pdMS_TO_TICKS(ms));
            break;
        }

        case DuckCmd::DEFAULT_DELAY:
            m_defaultDelay = atoi(cmd.param.c_str());
            break;

        case DuckCmd::DEFAULTCHARDELAY:
            m_defaultCharDelay = atoi(cmd.param.c_str());
            break;

        default:
            // Single key fallback
            uint8_t mod = 0;
            uint8_t key = get_hid_keycode(0, &mod); // TODO: improve string->key mapping
            if (key && m_onKeySend) {
                m_onKeySend(key, mod, true);
                vTaskDelay(pdMS_TO_TICKS(20));
                m_onKeySend(key, mod, false);
            }
    }
}

void DuckyParser::setOnKeySend(std::function<void(uint8_t, uint8_t, bool)> cb) {
    m_onKeySend = cb;
}

void DuckyParser::setOnDelay(std::function<void(uint32_t)> cb) {
    m_onDelay = cb;
}

} // namespace RubberDucky