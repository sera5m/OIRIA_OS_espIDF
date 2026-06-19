// os_code/middle_layer/input/rubberducky/kb_rd.hpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include "os_code/middle_layer/input/keymap.hpp"

namespace RubberDucky {

enum class DuckCmd : uint8_t {
    REM,
    STRING,
    STRINGLN,
    DELAY,
    DEFAULT_DELAY,
    DEFAULTCHARDELAY,
    REPEAT,
    KEYDOWN,
    KEYUP,
    HOLD,
    RELEASE,
    RESET,
};

struct DuckyCommand {
    DuckCmd cmd;
    std::string param;
    uint8_t repeat_count = 1;
};

class DuckyParser {
public:
    DuckyParser();
    ~DuckyParser();

    bool loadFile(const char* filename);        // SD card support
    bool parse(const std::string& content);
    void execute();                             // blocking execution
    void stop() { m_running = false; }
    bool isRunning() const { return m_running; }

    void setOnKeySend(std::function<void(uint8_t hid_code, uint8_t modifiers, bool pressed)> callback);
    void setOnDelay(std::function<void(uint32_t ms)> callback);

private:
    bool parseLine(const std::string& line);
    void executeCommand(const DuckyCommand& cmd);

    std::vector<DuckyCommand> m_commands;
    std::function<void(uint8_t, uint8_t, bool)> m_onKeySend;
    std::function<void(uint32_t)> m_onDelay;

    bool m_running = false;
    uint32_t m_defaultDelay = 100;
    uint32_t m_defaultCharDelay = 5;
};

} // namespace RubberDucky