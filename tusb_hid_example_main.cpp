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
#include "esp_vfs_fat.h"
#include "hal/i2c_types.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"


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

// Globals
/*
static ky040_handle_t enc_left = nullptr;
static ky040_handle_t enc_right = nullptr;
static int32_t ticks_left = 0;
static int32_t ticks_right = 0;
*/
spi_device_handle_t spi_lcd = nullptr;





// Encoder callback
/*

static void IRAM_ATTR on_encoder_event(void* ctx, int delta) {
    uintptr_t which = (uintptr_t)ctx;
    if (which == 0) ticks_left += delta;
    else            ticks_right += delta;
    ESP_LOGI(TAG, "%s encoder: %+d", which ? "Right" : "Left", delta);
}*/

// ────────────────────────────────────────────────
// Function prototypes (must come before app_main!)
static esp_err_t stage_1_encoders(void);
static esp_err_t stage_2_i2c_scan(void);
static esp_err_t boot_stage2andaHalf(void);
static esp_err_t stage_3_spi_init(void);
static esp_err_t stage_3_sd_mount(void);
static void       task_app_manager(bool sd_mounted);

// ────────────────────────────────────────────────









// Entry point
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "===== ESP32-S3 Boot =====");

    // === Stage 1: Encoders FIRST ===
    stage_1_encoders();                     // ← create devices + add to manager

    // === INPUT SYSTEM INITIALISATION (now safe) ===
    ProcInputQueTarget = xQueueCreate(32, sizeof(InputEvent));
    if (ProcInputQueTarget == nullptr) {
        ESP_LOGE(TAG, "Failed to create input queue");
    } else {
        ESP_LOGI(TAG, "Input queue created");
    }

    // Start consumer task AFTER devices exist
    startInputHandlerTask();

    // Continue with rest of boot
   // stage_2_i2c_scan();
    boot_stage2andaHalf();
    stage_3_spi_init();
    stage_3_sd_mount();
    task_app_manager(false);   // sd_ok was false anyway
}

// ────────────────────────────────────────────────
// Stage definitions (now after prototypes and app_main)

static esp_err_t stage_1_encoders()
{
    // Left encoder (Vertical)
    auto left_knob = std::make_unique<KnobDevice>();
    left_knob->props.cw_key  = KEY_DOWN;
    left_knob->props.ccw_key = KEY_UP;
    left_knob->props.button_key = KEY_ENTER;   // ← button press = ENTER

    ky040_config_t cfg_left = {
        .clk_pin = ENCODER0_CLK_PIN,
        .dt_pin  = ENCODER0_DT_PIN,
        .sw_pin  = ENCODER0_SW_PIN,
        .detents_per_rev = 20,
        .on_twist = nullptr,
        .user_ctx = nullptr
    };
    left_knob->initialize(&cfg_left);
    deviceManager.addDevice(std::move(left_knob));

    // Right encoder (Horizontal)
    auto right_knob = std::make_unique<KnobDevice>();
    right_knob->props.cw_key  = KEY_RIGHT;
    right_knob->props.ccw_key = KEY_LEFT;
right_knob->props.button_key = KEY_BACK; 

    ky040_config_t cfg_right = {
        .clk_pin = ENCODER1_CLK_PIN,
        .dt_pin  = ENCODER1_DT_PIN,
        .sw_pin  = ENCODER1_SW_PIN,
        .detents_per_rev = 20,
        .on_twist = nullptr,
        .user_ctx = nullptr
    };
    right_knob->initialize(&cfg_right);
    deviceManager.addDevice(std::move(right_knob));

    ESP_LOGI("ENCODERS", "Encoders registered successfully");
    return ESP_OK;
}



static esp_err_t stage_2_i2c_scan(void)
{
    constexpr gpio_num_t SCL = GPIO_NUM_8;
    constexpr gpio_num_t SDA = GPIO_NUM_9;

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = SDA,
        .scl_io_num = SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = false,
            .allow_pd = false
        }
    };

    i2c_master_bus_handle_t bus = nullptr;

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus creation failed");
        return ret;
    }

    // -----------------------------
    // UI INIT
    // -----------------------------
    fb_clear(0x0000);

    fb_draw_text(4, 25, 169, "i2c scan", 0xF00F, 2,
         0, true, 0x0000, 40, ft_AVR_classic_6x8);

    // Grid config - 4×4 = 16 circles, one per low nibble (v = 0-F)
    const int spacing_x = 32;
    const int spacing_y = 32;
    const int start_x = 128;
    const int start_y = (64);

    // -----------------------------
    // SCAN SUPERBLOCKS (one n at a time)
    // -----------------------------
    for (uint8_t n = 0; n <= 7; ++n)
    {
        // --- Draw superblock label "0xN-" ---
        char label_str[5] = "0x0-";
        label_str[2] = '0' + n;
        fb_draw_text(4, 128, 169, label_str, 0xFFFF, 3,
             0, true,
             0x0000, 40, ft_AVR_classic_6x8);

        // --- Draw fresh 4×4 grid (all gray = pending) ---
        for (int i = 0; i < 16; ++i)
        {
            int row = i / 4;
            int col = i % 4;

            int x = start_x + col * spacing_x;
            int y = start_y + row * spacing_y;

            fb_circle(x, y, 8,
                      shapefillpattern::plain,
                      0x8410, // gray
                      0xFFFF);
        }

        lcd_refresh_screen();

        // --- Scan the 16 addresses in this superblock ---
        for (int v = 0; v < 16; ++v)
        {
            uint8_t addr = (n << 4) | v;

            uint16_t color = 0x08F1; // red = no device (default)

            if (addr >= 0x08 && addr <= 0x77)
            {
                ret = i2c_master_probe(bus, addr, pdMS_TO_TICKS(30));

                if (ret == ESP_OK)
                {
                    color = 0x02FF; // green = device found
                    ESP_LOGI(TAG, "✓ Found device at 0x%02X", addr);
                }
            }
            // else: addresses outside official I2C range stay red (no device)

            // Update only this one circle (live)
            int row = v / 4;
            int col = v % 4;
            int x = start_x + col * spacing_x;
            int y = start_y + row * spacing_y;

            fb_circle(x, y, 16,
                      shapefillpattern::plain,
                      color,
                      0xFFFF);

            lcd_refresh_screen(); // ← LIVE UPDATE
			//maybe i should add some moving lines or somethigng to look cool idk
           // vTaskDelay(pdMS_TO_TICKS(8)); // small animation delay
        }

        // After finishing all v for this n, the next iteration will
        // automatically switch to the next superblock (new label + fresh gray grid)
    }

    i2c_del_master_bus(bus);

    ESP_LOGI(TAG, "I2C scan complete");
    return ESP_OK;
}



static esp_err_t boot_stage2andaHalf(void){
	 vTaskDelay(100);
	 framebuffer_alloc();
    vTaskDelay(100);
    
   // if(framebuffer){
		return ESP_OK;
		//}
}

static esp_err_t stage_3_spi_init(void) {
    // LCD GPIOs
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LCD_DC) | (1ULL << LCD_RST) | (1ULL << lcd_BL),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    gpio_set_direction(SPI_CS_LCD, GPIO_MODE_OUTPUT);
    gpio_set_level(SPI_CS_LCD, 1);
    gpio_set_level(LCD_RST, 1);
    gpio_set_level(LCD_DC, 0);
    gpio_set_level(lcd_BL, 1);

    // LCD bus
    spi_bus_config_t lcd_bus = {};
    lcd_bus.mosi_io_num = SPI_MOSI;
    lcd_bus.miso_io_num = -1;
    lcd_bus.sclk_io_num = SPI_CLK;
    lcd_bus.max_transfer_sz = SCREEN_W * SCREEN_H * 2 + 32;
    lcd_bus.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS;
    lcd_bus.isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO;
    lcd_bus.intr_flags = ESP_INTR_FLAG_IRAM;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &lcd_bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t lcd_dev = {};
    lcd_dev.clock_speed_hz = 76000000;
    lcd_dev.mode = 0;
    lcd_dev.spics_io_num = SPI_CS_LCD;
    lcd_dev.queue_size = 3;
    lcd_dev.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &lcd_dev, &spi_lcd));

    // SD bus (separate)
    spi_bus_config_t sd_bus = {};
    sd_bus.mosi_io_num = SD_MOSI;
    sd_bus.miso_io_num = SD_MISO;
    sd_bus.sclk_io_num = SD_SCK;
    sd_bus.max_transfer_sz = 8192;
    sd_bus.flags = SPICOMMON_BUSFLAG_MASTER;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &sd_bus, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "SPI buses ready");
    return ESP_OK;
}

static esp_err_t stage_3_sd_mount(void) {
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI2_HOST;
    slot.gpio_cs = SPI_CS_SD;

    esp_vfs_fat_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    sdmmc_card_t *card = nullptr;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    esp_err_t ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot, &mount, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD failed: %s (0x%x) – check 10k pull-ups on MOSI/MISO/CS!", 
                 esp_err_to_name(ret), ret);
        return ret;
    }

    ESP_LOGI(TAG, "SD OK");
    sdmmc_card_print_info(stdout, card);
    return ESP_OK;
}


//handles some boot stuff and will also update sensors (not input, it's got it's own task)
static void main_app_handles(bool sd_mounted) {
	//added delays to stop weird timing from causing crashes
	vTaskDelay(100);
    screen_set_driver(&onboard_screen_driver);
    vTaskDelay(100);
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
  	vTaskDelay(100);
    lcd_fb_display_framebuffer(false, false);
    vTaskDelay(pdMS_TO_TICKS(200));
    stage_2_i2c_scan();
    fb_clear(0x0000);
    //start application manager object, we will tick and own it inside this task
	auto& manager = appManager::instance();
	vTaskDelay(8);
	ApplicationConfig cfg;
		cfg.capabilities = static_cast<uint32_t>(AppCapability::FULLSCREEN) |
                   static_cast<uint32_t>(AppCapability::NEEDS_WINDOW);
		cfg.stack_size_bytes = 8192;
		cfg.priority = 5;
		cfg.name = "WatchApp";

auto app = std::make_shared<MyWatchApp>(cfg);
app->init();            // ✅ phase 2: registration with appManager
app->start_task();      // ✅ phase 3: run the FreeRTOS tas
while (1) { //now handle sensor loops and update because it's main
    update_display_time(&v_env.displayTime); //updates system variable, pulled down by other items
    vTaskDelay(pdMS_TO_TICKS(10)); // 100 Hz is already plenty
}

}