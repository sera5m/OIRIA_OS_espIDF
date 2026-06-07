// os_code/middle_layer/input/rubberducky/kb_rd.hpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include "os_code/middle_layer/input/input_handler.hpp"

namespace RubberDucky {

// DuckyScript commands
enum class DuckCmd : uint8_t {
    REM,           // Comment
    STRING,        // Type string
    STRINGLN,      // Type string + Enter
    DELAY,         // Pause milliseconds
    DEFAULT_DELAY, // Set default delay between commands
    DEFAULTCHARDELAY, // Delay between characters
    REPEAT,        // Repeat last command
    KEYDOWN,       // Press and hold key
    KEYUP,         // Release key
    HOLD,          // Hold modifier
    RELEASE,       // Release modifier
    RESET,         // Release all keys
    
    // Media keys
    MK_VOLUP, MK_VOLDOWN, MK_MUTE, MK_PP, MK_STOP,
    MK_PREV, MK_NEXT, MK_REW, MK_FF,
    
    // Modifier combos
    CTRL, ALT, SHIFT, GUI, WINDOWS,
    CONTROL  // alias for CTRL
};

// Single command structure
struct DuckyCommand {
    DuckCmd cmd;
    std::string param;      // For STRING, DELAY values
    uint8_t repeat_count;   // For REPEAT
};

// Parser class
class DuckyParser {
public:
    bool loadFile(const char* filename);  // Load .duck file from FATFS
    bool parse(const std::string& content); // Parse from string
    void execute();                        // Execute parsed commands
    
    void setOnKeySend(std::function<void(uint8_t keycode, bool pressed)> callback);
    void setOnDelay(std::function<void(uint32_t ms)> callback);
    
    bool isRunning() const { return m_running; }
    void stop() { m_running = false; }
    
private:
    bool parseLine(const std::string& line);
    void executeCommand(const DuckyCommand& cmd);
    void sendKeypress(uint8_t keycode, bool with_shift = false);
    void sendModifierCombo(uint8_t modifier, uint8_t keycode);
    
    std::vector<DuckyCommand> m_commands;
    std::function<void(uint8_t, bool)> m_onKeySend;
    std::function<void(uint32_t)> m_onDelay;
    bool m_running = false;
    uint32_t m_defaultDelay = 0;
    uint32_t m_defaultCharDelay = 0;
    DuckyCommand m_lastCommand;  // For REPEAT
};

// Key code mapping for DuckyScript
uint8_t duckyKeyToHID(const std::string& keyname);
uint8_t duckyModifierToHID(const std::string& modname);

} // namespace RubberDucky