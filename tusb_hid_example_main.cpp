// tusb_hid_example_main.cpp

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>   // for std::clamp, std::max, std::min

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "hal/i2c_types.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "os_code/middle_layer/input/hid_t.h"

#include "hardware/wiring/wiring.h"  // ← your pins!
#include "hardware/drivers/abstraction_layers/al_scr.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"

#include "hardware/drivers/psram_std/psram_std.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "driver/i2c_master.h"   // ← MUST have this (and esp_driver_i2c in CMakeLists.txt)
#include "os_code/middle_layer/input/input_handler.hpp"
#include "soc/gpio_num.h"

#include "os_code/middle_layer/input/input_devs_agg.hpp"
 
 #include "hardware/drivers/generic/button_driver.hpp"
#include "os_code/middle_layer/input/input_handler.hpp" 	
#include "hardware/drivers/encoders/ky040_driver.hpp"
#include "tusb.h"
#include "class/hid/hid.h"
#include <memory>
#include "freertos/FreeRTOS.h"      // ← MUST be the absolute first FreeRTOS include
#include "freertos/queue.h"
#include "freertos/task.h"

// Then other includes
#include "os_code/middle_layer/input/input_devs_agg.hpp"
#include <cstdio>
#include "code_stuff/types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <memory>
#include "esp_task_wdt.h"

#include "os_code/middle_layer/input/inputProscessorTask/ipt_x.hpp"
#include "os_code/core/rShell/enviroment/env_vars.h"


#include "os_code/core/rShell/s_hell.hpp"
#include "os_code/applications/watch/MS_watchapp.hpp"

#include "esp_task_wdt.h"
#include "esp_system.h"
#include "esp_cpu.h"
#include "esp_pm.h"

#include "tusb.h"
#include "class/hid/hid.h"


#include  "os_code/applications/fileviewwer/MS_file_viewwer.hpp"
#include "os_code/applications/watch/MS_watchapp.hpp"
#include  "os_code/applications/menu/app_menu.hpp"
#include "os_code/core/notification_sys/rs_notif_dispatcher.h"
#include "ulp_riscv.h"
//#include "ulp_riscv/ulp_riscv.h"
#include "sss/shared_state.h"   // from the ulp_component

//extern RTC_DATA_ATTR SharedState shared_state;

#include "Fboot/bootfunctions.hpp"


// Known devices (fill in your full list)
typedef struct {
    uint8_t addr;
    const char *name;
    const char *extra;
    int confidence;
} i2c_device_info_t;

static const i2c_device_info_t known_devices[] = {
    {0x3C, "SSD1306 / SH1106 / SSD1315", "0.96\" / 1.3\" OLED (common)", 98},
    // ... add the rest from your earlier list ...
};
#define NUM_KNOWN_DEVICES (sizeof(known_devices) / sizeof(known_devices[0]))
// ────────────────────────────────────────────────
//boot singleton
DeviceManager deviceManager;
QueueHandle_t ProcInputQueTarget = nullptr;

static const char* TAG = "main";

// Remove this old line completely:
// extern RTC_DATA_ATTR SharedState shared_state;

// Replace the old main_snotiync_from_ulp with:



// After other init
esp_err_t load_ulp(void) {
	
	
    extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
    extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");

    ESP_ERROR_CHECK(ulp_riscv_load_binary(ulp_main_bin_start,
        (ulp_main_bin_end - ulp_main_bin_start) / sizeof(uint32_t)));

    ulp_set_wakeup_period(0, 60 * 1000 * 1000);  // every 60 seconds (1 minute)
    ESP_ERROR_CHECK(ulp_riscv_run());

    shared_state_init();
    return ESP_OK;
}




spi_device_handle_t spi_lcd = nullptr;





// Encoder callback
/*

static void IRAM_ATTR on_encoder_event(void* ctx, int delta) {
    uintptr_t which = (uintptr_t)ctx;
    if (which == 0) ticks_left += delta;
    else            ticks_right += delta;
    ESP_LOGI(TAG, "%s encoder: %+d", which ? "Right" : "Left", delta);
}*/


static void       task_app_manager(bool sd_mounted);
static void bootloader_final_app(void);



// ────────────────────────────────────────────────

//extern retardtasks to fucking handle this the slur slur slur sahlur way
extern TaskHandle_t core2TaskHandle;





// Entry point
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "===== ESP32-S3 Boot =====");
    
    
    //conf cpu
esp_pm_config_t  pm_config = {
        .max_freq_mhz = v_env.cpuMhzMax,
        .min_freq_mhz = v_env.cpuMhzMin,
        .light_sleep_enable = false
    };
     esp_pm_configure(&pm_config);
    // === Stage 1: Encoders FIRST ===
   //boot this inside cpp main fuc fuck fuck stage_1_encoders();                     // ← create devices + add to manager



// In main, BEFORE stage_1_encoders():
ProcInputQueTarget = xQueueCreate(32, sizeof(InputEvent));
if (ProcInputQueTarget == nullptr) {
    ESP_LOGE(TAG, "Failed to create input queue");
} else {
    ESP_LOGI(TAG, "Input queue created");
}

// THEN start input task
startInputTask();

 stage_1_encoders(); 
    // Start consumer task AFTER devices exist
    

    // Continue with rest of boot
   // stage_2_i2c_scan();
    boot_stage2andaHalf();
    stage_3_spi_set(true);
    stage_3_sd_mount();
    
    
        // ====================== ULP ======================
        notification_system_init();
   // start_notification_task();

    // ====================== ULP RISC-V ======================

load_ulp();
    
    bootloader_final_app();   // sd_ok was false anyway
    
    
    
    
}

// ────────────────────────────────────────────────
// Stage definitions (now after prototypes and app_main)









//we are following a create-consume architecture for this (my idea, not the llm)
TaskHandle_t core1TaskHandle = NULL;
//moved core 2 task handle into the window enviroment but please trust me it exists


void core1_createData(void* pv) {
	esp_task_wdt_add(NULL); 
	
	
    while (1) {
		
        // Draw to framebuffer (which points to BACK buffer)
        update_display_time(&v_env.displayTime);
        esp_task_wdt_reset(); 
        //WindowManager::getInstance().UpdateAll(0,1,1,1); //no, this is bad why did we do this twice
        // Use false for repositioning when in fullscreen
        
        //=================render and shit====================
        
        
        if(!(v_env.headless)){ //if NOT headless, do this shit
        //drawing frame segment
WindowManager::getInstance().UpdateAll(false, true, true, true);
        esp_task_wdt_reset(); 
        // After drawing, swap so this frame becomes the FRONT buffer
    
	framebuffer_swap();           // make what we just drew the new front buffer
	g_display_dirty = true;
		//if (core2TaskHandle) {  		 xTaskNotifyGive(core2TaskHandle);		} //why tf this here twice?
        esp_task_wdt_reset(); 
        xTaskNotifyGive(core2TaskHandle);
        esp_task_wdt_reset(); 
        
        } // "so no head?"====================================
        
        
        //if we're doing the frames but struggling for perf we'll need to throttle to a target
        if ((v_env.UseFrameThrottle)){
			vTaskDelay(pdMS_TO_TICKS(1000 / (v_env.framethrottle_target)));
		}else{
		vTaskDelay(pdMS_TO_TICKS(1000 /( (v_env.fpsTarget))   )   );
		}
        //====================================
         //wait if headless or not because in no world do we need to update shit at 40+ fps
    }//while
    
    
}//end task



// Registration at file scope (runs before main)
REGISTER_APP(MyWatchApp, "WatchApp", make_watch_config);


//handles some boot stuff and will also update sensors (not input, it's got it's own task)
static void bootloader_final_app() {
	
	

	
	//added delays to stop weird timing from causing crashes
	vTaskDelay(50);
    screen_set_driver(&onboard_screen_driver);
    vTaskDelay(50);
	   //moved framebuffer alloc earlier to not interfere with spi
	   ESP_LOGI(TAG, "attempting lcd init");
   lcd_init_simple();
vTaskDelay(100);
ESP_LOGI(TAG, "invoking fb clear");
 fb_clear(0x0000);
  fb_draw_text(4, 20, 80, "booting", 0xFFFF, 2,
   0, true, 0x0000,
    40,ft_AVR_classic_6x8 );
  	ESP_LOGI(TAG, "invoking fb display framebuffer -total mode");
  	vTaskDelay(50);
  	refreshScreen();
    //lcd_fb_display_framebuffer(false, false);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    
    //--------------------i'll just skip this it takes forever with all my testing
  //  stage_2_i2c_scan();
  //-------------------------
 
   vTaskDelay(pdMS_TO_TICKS(50));
    fb_clear(0x0000);
        // Initialize WindowManager
    auto& wm = WindowManager::getInstance();
ESP_LOGI(TAG, "WindowManager init-d");
    
    ESP_LOGI(TAG, "WindowManager created");
    
    //start application manager object, we will tick and own it inside this task
	auto& manager = appManager::instance();
    vTaskDelay(8);
    
        v_env.CurrentHIDTarget=(HIDTarget)HIDTarget::toTaskAndDebug; //for now we'll use debug too. this is position 7. see hid_t.h if this doesn't work right

    
    
    // Create or get the instance first
auto watchapp = appManager::instance().launch_app("WatchApp");  // This creates and starts it
if (watchapp) {
    appManager::instance().set_focused_app(watchapp);
}



    
    
    //added dynamic throttle because display overperforms above target to ease cpu
const TickType_t targetTicks = pdMS_TO_TICKS(1000 / v_env.fpsTarget);

TickType_t lastWakeTime = xTaskGetTickCount();
//esp_task_wdt_add(NULL); //null means this task. as this task is the heavy refresh one, we need to add it so we can manually feed watchdog
//esp_task_wdt_set_timeout(6); //default five, i think? 

//Increase watchdog timeout
// In app_main() or bootloader_final_app() before creating tasks:
esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 10000,  // 10 seconds instead of 5
    .idle_core_mask = (1 << 0) | (1 << 1),  // monitor both idle tasks
    .trigger_panic = true
};
esp_task_wdt_reconfigure(&wdt_config);


g_display_mutex = xSemaphoreCreateMutex();//this is specifically for the core 2 display pusher, so we init it before the task is made itself and then use it


//create new tasks for proscessing loop-needed for video
xTaskCreatePinnedToCore(core1_createData, "core1", 8192, NULL, 5, &core1TaskHandle, 1);
launchTHESTUPIDMOTHERFUCKINGPEICEOFSHITDISPLAYPUSHTASKFUCKYOU();

// Register MenuApp

REGISTER_APP(app_launcher_menu, "MenuApp", make_menu_config); //don't worry, we declared this in the menu app




vTaskDelete(NULL); //KILL YOURSELF, BOOTLOADER! 
}




//you need to kill yourself NOW,bootloader, your life is as useless as a summer ant.......
/*
you serve ONE purpose
⣿⣿⣿⣿⣿⣿⣿⣿⣿⠏⠄⠄⠄⠄⠄⠄⠄⠄⠙⢿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⠄⠄⢀⣀⣀⣀⡀⠄⢀⣠⡔⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣰⢿⣿⣿⣿⣿⣿⣿⣷⡆⢠⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⡏⣻⣟⣿⣿⡿⣟⣛⣿⡃⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣧⣿⣾⣿⣷⣿⣷⣿⣿⣿⣷⣽⣹⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⡟⣟⣿⣿⠺⣟⣻⣿⣿⣿⡏⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⢿⡝⠻⠵⠿⠿⢿⣿⣿⢳⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣯⣧⠈⣛⣛⣿⣿⡿⣡⣞⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡧⠄⠙⠛⠛⢁⣴⣿⣿⣷⣿⢿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⡿⠟⠉⠄⠄⢠⠄⣀⣠⣾⣿⣿⡿⠟⠁⠄⠈⠛⢿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⡟⠉⠄⠄⢀⠠⠐⠒⠐⠾⠿⢟⠋⠁⠄⢀⣀⠠⠐⠄⠂⠈⠻⢿⣿⣿
⣿⣿⣿⠋⠁⠄⢀⡈⠄⠄⠄⠄⠄⠄⠄⠄⠁⠒⠉⠄⢠⣶⠄⠄⠄⠄⠄⠈⠫⢿
⣿⣿⡟⠄⢔⠆⡀⠄⠈⢀⠄⠄⠄⠄⠄⠄⠄⢄⡀⠄⠈⡐⢠⠒⠄⠄⠄⠄⢀⣂
⣿⣿⠁⡀⠄⠄⢇⠄⠄⢈⠆⠄⠄⢀⠔⠉⠁⠉⠉⠣⣖⠉⡂⡔⠂⠄⢀⠔⠁⠄
⣿⡿⠄⠄⠄⠄⢰⠹⣗⣺⠤⠄⠰⡎⠄⠄⠄⠄⠄⠄⠘⢯⡶⢟⡠⠰⠄⠄⠄⠄
*/

/*while (1) {
	
    update_display_time(&v_env.displayTime);
    WindowManager::getInstance().UpdateAll(0,1,1,1);
    //fb_clear(0xB1C8);
    
    esp_task_wdt_reset();//reset between creating the data and pushing to the screen, because each step is a heavy blocking task for this core
    //this may need to have substantial changes in the future for stability
    vTaskDelay(pdMS_TO_TICKS(1));
    refreshScreen();
    
	
    vTaskDelayUntil(&lastWakeTime, targetTicks);
}*/




//to-do add boot time counter and multi thread this