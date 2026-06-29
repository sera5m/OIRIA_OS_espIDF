/*
 * bootfunctions.cpp
 *
 * Extracted boot stage functions
 */

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
 //i don't give a fuck i'll just include every single thing. get fucked retards

static const char* TAG = "BOOT";






//storage and other shit
sdmmc_card_t *card = nullptr;





// ────────────────────────────────────────────────
// Original functions moved here
// ────────────────────────────────────────────────

 esp_err_t stage_1_encoders(void)
{
    ESP_LOGI("ENCODERS", "Initializing encoders and buttons...");

    // ========== LEFT ENCODER ==========
    auto left_knob = std::make_unique<KnobDevice>();
    left_knob->props.cw_key = KEY_DOWN;
    left_knob->props.ccw_key = KEY_UP;

    ky040_config_t cfg_left = {
        .clk_pin           = ENCODER0_CLK_PIN,
        .dt_pin            = ENCODER0_DT_PIN,
        .detents_per_rev   = 20,
        .on_twist          = nullptr,
        .user_ctx          = left_knob.get(),
        .use_interrupt     = 0,
        .on_pcnt_interrupt = encoder_isr_notify
    };

    left_knob->initialize(&cfg_left);
    gDeviceManager.addDevice(std::move(left_knob));

    // LEFT BUTTON
    auto left_btn = std::make_unique<ButtonDevice>();
    left_btn->props.press_key = KEY_ENTER;

    button_config_t btn_cfg_left = {
        .pin          = ENCODER0_SW_PIN,
        .active_low   = true,
        .debounce_ms  = 50,
        .on_press     = nullptr,
        .on_release   = nullptr,
        .user_ctx     = left_btn.get()
    };
    left_btn->initialize(&btn_cfg_left);
    gDeviceManager.addDevice(std::move(left_btn));

    // ========== RIGHT ENCODER ==========
    auto right_knob = std::make_unique<KnobDevice>();
    right_knob->props.cw_key = KEY_RIGHT;
    right_knob->props.ccw_key = KEY_LEFT;

    ky040_config_t cfg_right = {
        .clk_pin           = ENCODER1_CLK_PIN,
        .dt_pin            = ENCODER1_DT_PIN,
        .detents_per_rev   = 20,
        .on_twist          = nullptr,
        .user_ctx          = right_knob.get(),
        .use_interrupt     = 0,
        .on_pcnt_interrupt = encoder_isr_notify
    };

    right_knob->initialize(&cfg_right);
    gDeviceManager.addDevice(std::move(right_knob));

    // RIGHT BUTTON
    auto right_btn = std::make_unique<ButtonDevice>();
    right_btn->props.press_key = KEY_BACK;

    button_config_t btn_cfg_right = {
        .pin          = ENCODER1_SW_PIN,
        .active_low   = true,
        .debounce_ms  = 50,
        .on_press     = nullptr,
        .on_release   = nullptr,
        .user_ctx     = right_btn.get()
    };
    right_btn->initialize(&btn_cfg_right);
    gDeviceManager.addDevice(std::move(right_btn));

    ESP_LOGI("ENCODERS", "Encoders and buttons registered successfully");
    return ESP_OK;
}

 esp_err_t stage_2_i2c_scan(void){
	
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
         0, true, 0x0000,
          40, ft_AVR_classic_6x8);

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

        refreshScreen();

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

            refreshScreen(); // ← LIVE UPDATE
			//maybe i should add some moving lines or somethigng to look cool idk
           // vTaskDelay(pdMS_TO_TICKS(8)); // small animation delay
        }

        // After finishing all v for this n, the next iteration will
        // automatically switch to the next superblock (new label + fresh gray grid)
    }

   //i2c_del_master_bus(bus); //oop had this enabled, why did i do that! how silly
	fb_clear(0x0000);
	 refreshScreen();
    ESP_LOGI(TAG, "I2C scan complete");
    return ESP_OK;
}

 esp_err_t boot_stage2andaHalf(void){
    vTaskDelay(10);
    framebuffer_alloc();
    vTaskDelay(50);
    return ESP_OK;
}

 esp_err_t lcd_gpio_set(bool enable)
{
    if (enable) {
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
    } else {
        gpio_reset_pin(LCD_DC);
        gpio_reset_pin(LCD_RST);
        gpio_reset_pin(lcd_BL);
        gpio_reset_pin(SPI_CS_LCD);
    }
    return ESP_OK;
}

 esp_err_t lcd_spi_set(bool enable)
{
    if (enable) {
        spi_bus_config_t lcd_bus = {
            .mosi_io_num = SPI_MOSI,
            .miso_io_num = -1,
            .sclk_io_num = SPI_CLK,
            .max_transfer_sz = SCREEN_W * SCREEN_H * 2 + 32,
            .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
            .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
            .intr_flags = ESP_INTR_FLAG_IRAM,
        };

        spi_device_interface_config_t lcd_dev = {};
        lcd_dev.clock_speed_hz = 76000000;
        lcd_dev.mode = 0;
        lcd_dev.spics_io_num = SPI_CS_LCD;
        lcd_dev.queue_size = 3;
        lcd_dev.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY;

        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &lcd_bus, SPI_DMA_CH_AUTO));
        ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &lcd_dev, &spi_lcd));
    } else {
        if (spi_lcd) {
            ESP_ERROR_CHECK(spi_bus_remove_device(spi_lcd));
            spi_lcd = NULL;
        }
        ESP_ERROR_CHECK(spi_bus_free(SPI3_HOST));
    }
    return ESP_OK;
}

 esp_err_t sd_spi_set(bool enable)
{
    if (enable) {
        spi_bus_config_t sd_bus = {
            .mosi_io_num = SD_MOSI,
            .miso_io_num = SD_MISO,
            .sclk_io_num = SD_SCK,
            .max_transfer_sz = 8192,
            .flags = SPICOMMON_BUSFLAG_MASTER,
        };
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &sd_bus, SPI_DMA_CH_AUTO));
    } else {
        ESP_ERROR_CHECK(spi_bus_free(SPI2_HOST));
    }
    return ESP_OK;
}

 esp_err_t stage_3_spi_set(bool enable)
{
    if (enable) {
        ESP_ERROR_CHECK(lcd_gpio_set(true));
        ESP_ERROR_CHECK(lcd_spi_set(true));
        ESP_ERROR_CHECK(sd_spi_set(true));
        ESP_LOGI(TAG, "SPI buses ready");
    } else {
        ESP_ERROR_CHECK(sd_spi_set(false));
        ESP_ERROR_CHECK(lcd_spi_set(false));
        ESP_ERROR_CHECK(lcd_gpio_set(false));
        ESP_LOGI(TAG, "SPI buses shut down");
    }
    return ESP_OK;
}

 esp_err_t stage_3_sd_mount(void) {
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

   
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    esp_err_t ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot, &mount, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD failed: %s (0x%x)", esp_err_to_name(ret), ret);
        return ret;
    }

    ESP_LOGI(TAG, "SD OK");
    sdmmc_card_print_info(stdout, card);
    return ESP_OK;
}

// ────────────────────────────────────────────────
// New De-init / Re-init functions
// ────────────────────────────────────────────────

esp_err_t stage_3_spi_deinit(void)
{
    return stage_3_spi_set(false);
}

esp_err_t sd_unmount(void)
{
    esp_err_t ret = esp_vfs_fat_sdcard_unmount("/sdcard", card);  // Add the card pointer
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SD unmounted successfully");
    } else {
        ESP_LOGE(TAG, "SD unmount failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t sd_remount(void)
{
    esp_err_t ret = sd_unmount();  // This now takes no args
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unmount failed, continuing anyway");
    }
    vTaskDelay(pdMS_TO_TICKS(100));  // Give time for cleanup
    return stage_3_sd_mount();
}

esp_err_t lcd_deinit(void)
{
    lcd_gpio_set(false);
    lcd_spi_set(false);
    ESP_LOGI(TAG, "LCD deinitialized");
    return ESP_OK;
}

esp_err_t lcd_reinit(void)
{
    lcd_deinit();
    vTaskDelay(pdMS_TO_TICKS(50));
    lcd_gpio_set(true);
    lcd_spi_set(true);
    lcd_init_simple();
    ESP_LOGI(TAG, "LCD reinitialized");
    return ESP_OK;
}