// os_code/applications/toolkit/RemoteKeyboard/tk_rd.hpp
#pragma once

#include "os_code/core/rShell/application.hpp"
#include "os_code/middle_layer/input/rubberducky/kb_rd.hpp"

class RubberDuckyApp : public Application {
public:
    RubberDuckyApp();
    ~RubberDuckyApp() override;
    
    const char* getName() const override { return "RubberDucky"; }
    const char* getDescription() const override { return "DuckyScript payload executor"; }
    
    void on_attach() override;
    void on_detach() override;
    void on_tick() override;
    void on_draw() override;
    void on_input(const InputEvent& ev) override;
    
private:
    enum class State {
        IDLE,
        SELECTING_FILE,
        EXECUTING,
        PAUSED,
        COMPLETE
    };
    
    State m_state = State::IDLE;
    std::unique_ptr<RubberDucky::DuckyParser> m_parser;
    std::vector<std::string> m_scriptFiles;
    int m_selectedIndex = 0;
    uint32_t m_progress = 0;
};