// main.cpp  (from tusb_hid_example_main.cpp + os.conf boot load)

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>   // for std::clamp, std::max, std::min
#include "os_code/core/com/boot_role.hpp"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_err.h"
#include "hal/i2c_types.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "os_code/middle_layer/input/hid_t.h"

#include "hardware/wiring/wiring.h"
#include "hardware/drivers/abstraction_layers/al_scr.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"

#include "hardware/drivers/psram_std/psram_std.hpp"
#include "driver/i2c_master.h"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "soc/gpio_num.h"

#include "os_code/middle_layer/input/input_devs_agg.hpp"
#include "hardware/drivers/generic/button_driver.hpp"
#include "hardware/drivers/encoders/ky040_driver.hpp"
#include "tusb.h"
#include "class/hid/hid.h"
#include <memory>

#include "code_stuff/types.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_cpu.h"
#include "esp_pm.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "os_code/middle_layer/input/inputProscessorTask/ipt_x.hpp"
#include "os_code/core/rShell/rshell_appFramework.hpp"

#include "os_code/applications/watch/MS_watchapp.hpp"
#include "os_code/applications/discord/discord_rpc.h"
#include "os_code/applications/fileviewer/MS_file_viewer.hpp"
#include "os_code/applications/menu/app_menu.hpp"
#include "os_code/applications/pong/MS_pongapp.hpp"
#include "os_code/applications/snake/MS_snakeapp.hpp"
#include "os_code/applications/2048/MS_2048app.hpp"
#include "os_code/applications/browser/MS_browserapp.hpp"
#include "os_code/applications/vulcanApp/MS_vulcanapp.hpp"

#include "app_registerTable.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "os_code/core/com/rs_autoconnect.hpp"
#include "os_code/core/notification_sys/rs_notif_dispatcher.h"
#include "ulp_riscv.h"
#include "sss/shared_state.h"
#include "Fboot/bootfunctions.hpp"

// =============================================================================
// os.conf — boot-time OS config (see root os.conf / OS_CONF_NOTES.md)
// =============================================================================
// Load order: 1) /sdcard/conf/os.conf  2) embedded blob  3) keep v_env defaults
// Applies into EnvConfig (v_env). bootRole is also written to NVS "rshell"
// so boot_role_resolve() picks it up.
// =============================================================================

//#include "os.conf"
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







// Optional: link os.conf as EMBED_TXTFILES in CMakeLists so these symbols exist.
// Weak so firmware still links if the blob is not embedded yet.
extern const uint8_t _binary_os_conf_start[] __attribute__((weak));
extern const uint8_t _binary_os_conf_end[]   __attribute__((weak));

// Runtime power / link policy from conf (not all are in EnvConfig yet)
typedef enum {
    OS_POWER_NORMAL = 0,
    OS_POWER_DEEP_SLEEP,
    OS_POWER_STAY_AWAKE,
} os_power_mode_t;

typedef enum {
    OS_RC_PROTO_NONE = 0,
    OS_RC_PROTO_RSDOM,
    OS_RC_PROTO_UART_RAW,
} os_rc_proto_t;

typedef enum {
    OS_UART_CLIENT = 0,
    OS_UART_SERVER,
    OS_UART_PUPPET,
} os_uart_link_t;

struct OsConfExtras {
    os_power_mode_t power_mode;
    bool            is_remote_control;
    os_rc_proto_t   rc_proto;
    os_uart_link_t  uart_link;
    bool            boot_role_set;   // true if conf specified bootRole
    boot_role_t     boot_role;
};

static OsConfExtras g_os_extras = {
    OS_POWER_NORMAL, false, OS_RC_PROTO_NONE, OS_UART_CLIENT, false, BOOT_ROLE_SOLO
};

static void os_conf_trim(char* s) {
    if (!s) return;
    char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
    char* p = s;
    while (*p && isspace((unsigned char)*p)) ++p;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static bool os_conf_truthy(const char* v) {
    if (!v || !v[0]) return false;
    if (v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T') return true;
    if (!strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on")) return true;
    return false;
}

static void os_conf_apply_key(const char* key, const char* val) {
    if (!key || !val) return;

    // --- window / EnvConfig ---
    if (!strcmp(key, "fpsTarget")) {
        int v = atoi(val);
        if (v > 0 && v <= 240) v_env.fpsTarget = (uint16_t)v;
    } else if (!strcmp(key, "framethrottle_target")) {
        int v = atoi(val);
        if (v > 0 && v <= 240) v_env.framethrottle_target = (uint16_t)v;
    } else if (!strcmp(key, "UseFrameThrottle")) {
        v_env.UseFrameThrottle = os_conf_truthy(val);
    } else if (!strcmp(key, "brightness")) {
        int v = atoi(val);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        v_env.brightness = (uint8_t)v;
    } else if (!strcmp(key, "screen_dim_w")) {
        int v = atoi(val);
        if (v > 0 && v <= 1024) v_env.screen_dim_w = (uint16_t)v;
    } else if (!strcmp(key, "screen_dim_h")) {
        int v = atoi(val);
        if (v > 0 && v <= 1024) v_env.screen_dim_h = (uint16_t)v;
    } else if (!strcmp(key, "screenGamma")) {
        v_env.screenGamma = (float)atof(val);
    } else if (!strcmp(key, "headless")) {
        v_env.headless = os_conf_truthy(val);
    } else if (!strcmp(key, "debugMode")) {
        v_env.debugMode = os_conf_truthy(val);
    } else if (!strcmp(key, "safeMode")) {
        v_env.safeMode = os_conf_truthy(val);
    } else if (!strcmp(key, "cpuMhzTarget")) {
        int v = atoi(val);
        if (v >= 40 && v <= 240) v_env.cpuMhzTarget = (uint16_t)v;
    } else if (!strcmp(key, "enableCpuScaling")) {
        v_env.enableCpuScaling = os_conf_truthy(val);

    // --- power ---
    } else if (!strcmp(key, "powerMode")) {
        if (!strcasecmp(val, "deep_sleep"))      g_os_extras.power_mode = OS_POWER_DEEP_SLEEP;
        else if (!strcasecmp(val, "stay_awake")) g_os_extras.power_mode = OS_POWER_STAY_AWAKE;
        else                                     g_os_extras.power_mode = OS_POWER_NORMAL;

    // --- boot role / collab ---
    } else if (!strcmp(key, "bootRole")) {
        g_os_extras.boot_role_set = true;
        if (!strcasecmp(val, "tyrant"))      g_os_extras.boot_role = BOOT_ROLE_TYRANT;
        else if (!strcasecmp(val, "puppet")) g_os_extras.boot_role = BOOT_ROLE_PUPPET;
        else                                 g_os_extras.boot_role = BOOT_ROLE_SOLO;
    } else if (!strcmp(key, "isRemoteControl")) {
        g_os_extras.is_remote_control = os_conf_truthy(val);
    } else if (!strcmp(key, "remoteControlProtocol")) {
        if (!strcasecmp(val, "rsdom"))         g_os_extras.rc_proto = OS_RC_PROTO_RSDOM;
        else if (!strcasecmp(val, "uart_raw")) g_os_extras.rc_proto = OS_RC_PROTO_UART_RAW;
        else                                   g_os_extras.rc_proto = OS_RC_PROTO_NONE;
    } else if (!strcmp(key, "uartLinkRole")) {
        if (!strcasecmp(val, "server"))      g_os_extras.uart_link = OS_UART_SERVER;
        else if (!strcasecmp(val, "puppet")) g_os_extras.uart_link = OS_UART_PUPPET;
        else                                 g_os_extras.uart_link = OS_UART_CLIENT;
    } else {
        ESP_LOGD(TAG, "os.conf: unknown key '%s'", key);
    }
}

// Parse flat key = value text. Lines starting with # or blank are ignored.
// Inline # comments are stripped after the value.
static bool os_conf_parse(const char* text) {
    if (!text) return false;
    char line[192];
    const char* p = text;
    int applied = 0;

    while (*p) {
        // copy one line
        size_t n = 0;
        while (*p && *p != '\n' && *p != '\r' && n + 1 < sizeof(line)) {
            line[n++] = *p++;
        }
        line[n] = 0;
        while (*p == '\n' || *p == '\r') ++p;

        os_conf_trim(line);
        if (line[0] == 0 || line[0] == '#') continue;

        // strip trailing inline comment (first unquoted #)
        char* hash = strchr(line, '#');
        if (hash) {
            *hash = 0;
            os_conf_trim(line);
            if (line[0] == 0) continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char* key = line;
        char* val = eq + 1;
        os_conf_trim(key);
        os_conf_trim(val);
        // strip optional quotes around value
        size_t vl = strlen(val);
        if (vl >= 2 && ((val[0] == '"' && val[vl - 1] == '"') ||
                        (val[0] == '\'' && val[vl - 1] == '\''))) {
            val[vl - 1] = 0;
            ++val;
        }
        if (key[0] == 0) continue;

        os_conf_apply_key(key, val);
        ++applied;
    }

    ESP_LOGI(TAG, "os.conf: applied %d key(s)", applied);
    return applied > 0;
}

// Write bootRole into NVS so boot_role_resolve() sees it (same key as boot_role.c).
static void os_conf_commit_boot_role_nvs(void) {
    if (!g_os_extras.boot_role_set) return;
    nvs_handle_t h;
    if (nvs_open("rshell", NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "os.conf: NVS open failed (boot_role not stored)");
        return;
    }
    esp_err_t e = nvs_set_u8(h, "boot_role", (uint8_t)g_os_extras.boot_role);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e == ESP_OK) {
        ESP_LOGI(TAG, "os.conf: NVS boot_role = %s", boot_role_name(g_os_extras.boot_role));
    } else {
        ESP_LOGW(TAG, "os.conf: NVS write boot_role failed (%s)", esp_err_to_name(e));
    }
}

// Apply power policy after conf load (light_sleep / stay awake).
static void os_conf_apply_power(void) {
    esp_pm_config_t pm = {
        .max_freq_mhz = v_env.cpuMhzMax ? v_env.cpuMhzMax : 240,
        .min_freq_mhz = v_env.cpuMhzMin ? v_env.cpuMhzMin : 80,
        .light_sleep_enable = false
    };
    if (g_os_extras.power_mode == OS_POWER_DEEP_SLEEP) {
        // Keep light_sleep off here; deep sleep is entered later by idle policy.
        // Reflect intent in logs; actual esp_deep_sleep is policy-owned.
        ESP_LOGI(TAG, "os.conf: powerMode=deep_sleep (idle policy owns sleep entry)");
    } else if (g_os_extras.power_mode == OS_POWER_STAY_AWAKE) {
        pm.light_sleep_enable = false;
        ESP_LOGI(TAG, "os.conf: powerMode=stay_awake");
    } else {
        ESP_LOGI(TAG, "os.conf: powerMode=normal");
    }
    if (v_env.cpuMhzTarget >= 40 && v_env.cpuMhzTarget <= 240) {
        pm.max_freq_mhz = v_env.cpuMhzTarget;
    }
    esp_err_t e = esp_pm_configure(&pm);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "os.conf: esp_pm_configure failed (%s)", esp_err_to_name(e));
    }
}

// Load SD path first, then embedded factory blob. Does not clobber keys not present.
static bool os_conf_load(const char* sd_path) {
    if (!sd_path) sd_path = "/sdcard/conf/os.conf";
    bool ok = false;

    // 1) SD
    FILE* f = fopen(sd_path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz < 16384) {
            char* buf = (char*)malloc((size_t)sz + 1);
            if (buf) {
                size_t n = fread(buf, 1, (size_t)sz, f);
                buf[n] = 0;
                fclose(f);
                ok = os_conf_parse(buf);
                free(buf);
                if (ok) {
                    ESP_LOGI(TAG, "os.conf: loaded from %s", sd_path);
                    os_conf_commit_boot_role_nvs();
                    return true;
                }
            } else {
                fclose(f);
            }
        } else {
            fclose(f);
        }
    }

    // 2) Embedded (CMake: target_add_binary_data(... os.conf TEXT) → _binary_os_conf_*)
    if (&_binary_os_conf_start[0] != &_binary_os_conf_end[0] &&
        _binary_os_conf_end > _binary_os_conf_start) {
        size_t n = (size_t)(_binary_os_conf_end - _binary_os_conf_start);
        char* buf = (char*)malloc(n + 1);
        if (buf) {
            memcpy(buf, _binary_os_conf_start, n);
            buf[n] = 0;
            ok = os_conf_parse(buf);
            free(buf);
            if (ok) {
                ESP_LOGI(TAG, "os.conf: loaded embedded (%u bytes)", (unsigned)n);
                os_conf_commit_boot_role_nvs();
                return true;
            }
        }
    }

    ESP_LOGW(TAG, "os.conf: no file (SD or embed) — using EnvConfig defaults");
    return false;
}







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




//forward declarations for further bootfuncts
//void register_watch();
//void register_menu();





// Entry point

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "===== ESP32-S3 Boot =====");

    // Early PM from compile defaults; os.conf may reconfigure after load.
    esp_pm_config_t pm_config = {
        .max_freq_mhz = v_env.cpuMhzMax,
        .min_freq_mhz = v_env.cpuMhzMin,
        .light_sleep_enable = false
    };
    esp_pm_configure(&pm_config);

    ProcInputQueTarget = xQueueCreate(32, sizeof(InputEvent));
    if (ProcInputQueTarget == nullptr) {
        ESP_LOGE(TAG, "Failed to create input queue");
    } else {
        ESP_LOGI(TAG, "Input queue created");
    }

    startInputTask();
    stage_1_encoders();
    boot_stage2andaHalf();
    stage_3_spi_set(true);
    stage_3_sd_mount();

    // --- os.conf after SD is up, before role / display / apps ---
    // Ensures boot_role NVS write is visible to boot_role_resolve() in final app.
    os_conf_load("/sdcard/conf/os.conf");
    os_conf_apply_power();

    notification_system_init();
    load_ulp();

    bootloader_final_app();
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


//we might havea to
extern "C" void rs_dom_link_start_tx(void);  // puppet
extern "C" void rs_dom_link_start_rx(void);  // tyrant




extern  const char* kTestSsid;
extern  const char* kTestPass;
extern  const char* kDefaultWebhook;
extern  const char* kDefaultMsg;

// ---------------------------------------------------------------------------
// Discord webhook test (inline in main for now)
// ---------------------------------------------------------------------------




static bool ensure_wifi(void) {
    ESP_LOGI(TAG, "wifi: preparing STA (ssid='%s')", kTestSsid);

    rs_ac_table_t tab{};
    rs_ac_set_test_wifi(&tab, kTestSsid, kTestPass);
    rs_ac_sync_to_env(&tab);

    ESP_LOGI(TAG, "wifi: connecting (timeout 15s, WPA2/WPA3)...");
    int64_t t0 = esp_timer_get_time();
    bool ok = rs_ac_wifi_connect(&tab, 25000);
    int64_t ms = (esp_timer_get_time() - t0) / 1000;

    if (ok) {
        ESP_LOGI(TAG, "wifi: CONNECTED ssid='%s' in %lld ms", kTestSsid, (long long)ms);
    } else {
        ESP_LOGW(TAG, "wifi: FAILED ssid='%s' after %lld ms — POST may still work if already online",
                 kTestSsid, (long long)ms);
    }
    return ok;
}






//handles some boot stuff and will also update sensors (not input, it's got it's own task)



static void bootloader_final_app() {
    vTaskDelay(pdMS_TO_TICKS(50));

    // boot_role_resolve sees NVS written by os_conf_load if conf set bootRole
    const boot_role_t role = boot_role_resolve();
    v_env.headless = !boot_has_display(role);

    ESP_LOGI(TAG, "=== final boot role: %s (headless=%d) ===",
             boot_role_name(role), (int)v_env.headless);
    ESP_LOGI(TAG, "os.conf extras: power=%d remote=%d rc_proto=%d uart_link=%d fps=%u",
             (int)g_os_extras.power_mode,
             (int)g_os_extras.is_remote_control,
             (int)g_os_extras.rc_proto,
             (int)g_os_extras.uart_link,
             (unsigned)v_env.fpsTarget);

    if (boot_has_display(role)) {
        screen_set_driver(&onboard_screen_driver);
        vTaskDelay(pdMS_TO_TICKS(50));
        ESP_LOGI(TAG, "LCD init");
        lcd_init_simple();
        vTaskDelay(pdMS_TO_TICKS(100));
        fb_clear(0x0000);
        fb_draw_text(4, 20, 80, "booting", 0xFFFF, 2,
                     0, true, 0x0000, 40, ft_AVR_classic_6x8);
        vTaskDelay(pdMS_TO_TICKS(50));
        refreshScreen();
        vTaskDelay(pdMS_TO_TICKS(50));
        fb_clear(0x0000);

        auto& wm = WindowManager::getInstance();
        (void)wm;
        ESP_LOGI(TAG, "WindowManager ready");

        g_display_mutex = xSemaphoreCreateMutex();
        launchTHESTUPIDMOTHERFUCKINGPEICEOFSHITDISPLAYPUSHTASKFUCKYOU();
    } else {
        ESP_LOGI(TAG, "headless: skip LCD / WindowManager display path");
        (void)WindowManager::getInstance();
    }

    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 10000,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true
    };
    esp_task_wdt_reconfigure(&wdt_config);

    xTaskCreatePinnedToCore(core1_createData, "core1", 8192, NULL, 5, &core1TaskHandle, 1);

    if (boot_sends_dom(role)) {
        ESP_LOGI(TAG, "starting DOM TX (puppet → tyrant)");
        rs_dom_link_start_tx();
    }
    if (boot_receives_dom(role)) {
        ESP_LOGI(TAG, "starting DOM RX (tyrant ← puppets)");
        rs_dom_link_start_rx();
    }

    auto& manager = appManager::instance();
    manager.start_manager_task();
    vTaskDelay(pdMS_TO_TICKS(8));

    v_env.CurrentHIDTarget = (HIDTarget)HIDTarget::toTaskAndDebug;

    Register_appTable();

    if (boot_runs_apps_locally(role)) {
        auto watchapp = manager.open_app("WatchApp");
        if (!watchapp) {
            ESP_LOGE(TAG, "Failed to launch WatchApp");
        }
    } else {
        ESP_LOGI(TAG, "tyrant: waiting for puppet DOM (no local WatchApp)");
    }

    send_discord_rpc_msg("THEY GLOW YOU SHINE");

    vTaskDelay(pdMS_TO_TICKS(8));
    vTaskDelete(NULL);
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
