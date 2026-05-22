#pragma once
#include <stdint.h>
#include "esp_timer.h"
#include "hardware/drivers/lcd/fonts/font_basic_types.h"
#include <string>
#include <memory>
#include <sstream>
#include <algorithm>
#include <variant>
#include "code_stuff/types.h"
#include <math.h>
#include "hardware/wiring/wiring.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "rom/cache.h"
#include <string.h>
#include "hardware/drivers/abstraction_layers/al_scr.h"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "os_code/core/window_env/wenv_basicThemes.h"
#include <vector>
#include "../../../hardware/drivers/psram_std/psram_std.hpp"
#include "hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "os_code/core/rShell/s_hell.hpp"
#include "os_code/core/window_env/MWenv.hpp"

//the file viewwer app draws from the file m nagement app. 
//this was intended to take some inspiration from listary, using an indexed table, but we're so constrained on psram it's diabolical


//THE distinction between these two modes is that f v_app mode is what we do with closed files, 
//and u_mode is use mode, and what is open file doing

typedef enum{
    FV_MAIN, //entrypoint
     FV_SEARCHING, //actively searching for the file
     FV_OPENING, //opening a selected file
     FV_CLOSING, //closing the file
     FV_IDLE_VIEW, //we're looking at a file
     FV_IDLE_TRAVERSING, //the user is looking for a file
     FV_IDLE, //just sitting here with the directory open
     FV_MOVING, 
     FV_DELETING,
     FV_COPYING,
     FV_COUNT
    }FV_APP_Mode;


    //open file viewwer mode [moved to driver]
   

        typedef enum{//file viewwer use mode
            FVU_load,FVU_save, //it takes some time to pull the file out of memory
            FVU_editing, //we're fucking with it
            FVU_observing, //we are looking at it
            FVU_StateTransition //the file is transgender!(it's opening or closing). they call them trans rights because i have nothing left :(
           }FV_U_Mode; //file viewwer app use mode


class fv_app : public AppBase {
public:
    explicit fv_app(const ApplicationConfig& cfg);

    void tick_app(uint32_t delta_ms) override;
    void receive_event_input(const void* event) override;
    void suspend() override;
    void force_close() override;

    void on_start() override;
    void on_stop() override;
    void on_pause() override;
    void on_resume() override;
    void on_draw() override;

    //customs per this app
    

private:
    std::shared_ptr<Window> fv_app_window;
};