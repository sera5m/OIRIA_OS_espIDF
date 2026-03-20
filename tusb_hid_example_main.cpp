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
#include "sdmmc_cmd.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"

#include "hardware/wiring/wiring.h"  // ← your pins!
#include "hardware/drivers/abstraction_layers/al_scr.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include "hardware/drivers/encoders/ky040_driver.hpp"
#include "hardware/drivers/psram_std/psram_std.hpp"
#include "os_code/core/window_env/MWenv.hpp"

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

static const char* TAG = "main";

// Globals
static ky040_handle_t enc_left = nullptr;
static ky040_handle_t enc_right = nullptr;
static int32_t ticks_left = 0;
static int32_t ticks_right = 0;
spi_device_handle_t spi_lcd = nullptr;

// Encoder callback
static void IRAM_ATTR on_encoder_event(void* ctx, int delta) {
    uintptr_t which = (uintptr_t)ctx;
    if (which == 0) ticks_left += delta;
    else            ticks_right += delta;
    ESP_LOGI(TAG, "%s encoder: %+d", which ? "Right" : "Left", delta);
}

// ────────────────────────────────────────────────
// Function prototypes (must come before app_main!)
static esp_err_t stage_1_encoders(void);
static esp_err_t stage_2_i2c_scan(void);
static esp_err_t boot_stage2andaHalf(void);
static esp_err_t stage_3_spi_init(void);
static esp_err_t stage_3_sd_mount(void);
static void       stage_4_main_app(bool sd_mounted);

// ────────────────────────────────────────────────
// Entry point
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "===== ESP32-S3 Boot =====");

    bool sd_ok = false;  // 👈 declare early
/*
    if (stage_1_encoders() != ESP_OK) goto panic;
    if (stage_2_i2c_scan() != ESP_OK) goto panic;
    boot_stage2andaHalf(); //jus run this
    if (stage_3_spi_init() != ESP_OK) goto panic;

    sd_ok = (stage_3_sd_mount() == ESP_OK);  // 👈 assign later

    stage_4_main_app(sd_ok);  // never returns
*/
	stage_1_encoders();  stage_2_i2c_scan();  boot_stage2andaHalf(); stage_3_spi_init(); stage_3_sd_mount(); stage_4_main_app(sd_ok);




//panic:
   // ESP_LOGE(TAG, "Critical failure – halting");
  //  while (1) vTaskDelay(1000);
    
}

// ────────────────────────────────────────────────
// Stage definitions (now after prototypes and app_main)

static esp_err_t stage_1_encoders(void) {
    ky040_config_t cfg_left = {
        .clk_pin = ENCODER0_CLK_PIN,
        .dt_pin = ENCODER0_DT_PIN,
        .sw_pin = ENCODER0_SW_PIN,
        .detents_per_rev = 20,
        .on_twist = on_encoder_event,
        .user_ctx = (void*)0
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(ky040_new(&cfg_left, &enc_left));

    ky040_config_t cfg_right = {
        .clk_pin = ENCODER1_CLK_PIN,
        .dt_pin = ENCODER1_DT_PIN,
        .sw_pin = ENCODER1_SW_PIN,
        .detents_per_rev = 20,
        .on_twist = on_encoder_event,
        .user_ctx = (void*)1
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(ky040_new(&cfg_right, &enc_right));

    ESP_LOGI(TAG, "Encoders OK");
    return ESP_OK;
}

static esp_err_t stage_2_i2c_scan(void) {
    constexpr gpio_num_t SDA = GPIO_NUM_8;
    constexpr gpio_num_t SCL = GPIO_NUM_9;

    // Debug idle levels
    gpio_set_direction(SDA, GPIO_MODE_INPUT);
    gpio_set_direction(SCL, GPIO_MODE_INPUT);
    ESP_LOGI(TAG, "Idle SDA=%d SCL=%d (expect 1 with pull-ups)", 
             gpio_get_level(SDA), gpio_get_level(SCL));

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SDA,
        .scl_io_num = SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 4,
        .flags = { .enable_internal_pullup = 1, .allow_pd = false }
    };

    i2c_master_bus_handle_t bus = nullptr;
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Scanning...");
    int found = 0;

    for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
        if (i2c_master_probe(bus, addr, pdMS_TO_TICKS(500)) == ESP_OK) {
            found++;
            ESP_LOGI(TAG, "Found 0x%02X", addr);
            for (size_t i = 0; i < NUM_KNOWN_DEVICES; ++i) {
                if (known_devices[i].addr == addr) {
                    ESP_LOGI(TAG, " → %s (%s) [~%d%%]", 
                             known_devices[i].name, known_devices[i].extra, known_devices[i].confidence);
                    break;
                }
            }
        }
    }

    ESP_LOGI(TAG, "Scan done – %d devices", found);
    i2c_del_master_bus(bus);
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

static void stage_4_main_app(bool sd_mounted) {
	//added delays to stop weird timing from causing crashes
	vTaskDelay(100);
    screen_set_driver(&onboard_screen_driver);
    vTaskDelay(100);
	   //moved framebuffer alloc earlier to not interfere with spi
	   ESP_LOGI(TAG, "attempting lcd init");
   lcd_init_angry();
vTaskDelay(100);
ESP_LOGI(TAG, "invoking fb clear");
 fb_clear(0x0000);
  fb_draw_text(4, 20, 80, "booting", 0xFFFF, 2, avrclassic_font6x8, 0, true, 0x0000, 40, {6,8});
  	ESP_LOGI(TAG, "invoking fb display framebuffer -total mode");
  	vTaskDelay(100);
    lcd_fb_display_framebuffer(false, false);
    vTaskDelay(pdMS_TO_TICKS(200));

/*
    constexpr int MAX_FILES = 64;
    constexpr int MAX_NAME_LEN = 256;
    char file_list[MAX_FILES][MAX_NAME_LEN] = {};
    int file_count = 0;

    if (sd_mounted) {
        DIR* dir = opendir("/sdcard");
        if (dir) {
            struct dirent* e;
            while ((e = readdir(dir)) && file_count < MAX_FILES) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;

                char p[128];
                snprintf(p, sizeof(p), "/sdcard/%s", e->d_name);
                struct stat st{};
                bool is_dir = (stat(p, &st) == 0 && S_ISDIR(st.st_mode));

                snprintf(file_list[file_count], sizeof(file_list[0]),
                         "%s%s", is_dir ? "[DIR] " : " ", e->d_name);
                file_count++;
            }
            closedir(dir);
        }
    }

    if (!file_count) {
        strcpy(file_list[0], "No SD / empty");
        file_count = 1;
    }*/

    auto win = std::make_shared<Window>(
        WindowCfg{
            .Posx = 0,
            .Posy = 64,
            .win_width = 180,
            .win_height = 100,
            .win_rotation = 1,
            .borderless = 0,
            .TextSizeMult = 1,
            .BgColor = 0xBBBB,
            .WinTextColor = 0xFFFF,
            .UpdateRate = 1.0f
        },
        "meowwy"
    );
    

    // Boot splash
    fb_rect(0, 8, 25, 25, 100, 100, 0xFF34, 0x5432);
    display_framebuffer(true, false);
    fb_draw_ptext(4, 40, 80, stdpsram::String("PSRAM LINK READY"), 0xFFFF, 2, avrclassic_font6x8, 0, 0, 0x0000, 40, {6,8});
    lcd_fb_display_framebuffer(false, false);
    vTaskDelay(pdMS_TO_TICKS(50));
win->WinDraw();
        display_framebuffer(true, false);
    int sel = 0, scr = 0;
/*

    while (true) {
        if (enc_left) ky040_poll(enc_left);
        if (enc_right) ky040_poll(enc_right);

        bool redraw = false;
        if (ticks_left) { sel += ticks_left; ticks_left = 0; sel = std::clamp(sel, 0, file_count-1); redraw = true; }
        if (ticks_right) { scr += ticks_right * 3; ticks_right = 0; scr = std::clamp(scr, 0, std::max(0, file_count-12)); redraw = true; }

        if (!redraw) { vTaskDelay(pdMS_TO_TICKS(30)); continue; }

        stdpsram::String txt;
        txt.reserve(2048);
        txt += "<|size=1|><|b|>SD Card: /sdcard<|/b|><|/size|>\n\n";

        for (int i = scr; i < file_count && i < scr + 12; ++i) {
            if (i == sel) txt += "<|hl=0xF800|><|b|>" + stdpsram::String(file_list[i]) + "<|/b|><|/hl|>\n";
            else txt += stdpsram::String(file_list[i]) + "\n";
        }

        char foot[96];
        snprintf(foot, sizeof(foot), "\nItems: %d Sel: %d/%d <> sel ^v scroll", file_count, sel+1, file_count);
        txt += foot;

        win->SetText(txt);
        win->WinDraw();
        display_framebuffer(true, false);
        vTaskDelay(pdMS_TO_TICKS(30));
    }*/
}