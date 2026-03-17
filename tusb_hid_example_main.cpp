


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
//#include <stdint>
#include <string.h>
#include <math.h>
#include <vector>
#include "rom/cache.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"          // ← add this
#include "sdmmc_cmd.h"            // ← add this

#include "hardware/wiring/wiring.h"
#include "hardware/drivers/abstraction_layers/al_scr.h"
//my libs
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "hardware/drivers/lcd/fonts/font_basic_types.h"
#include "os_code/core/window_env/MWenv.hpp"
#include "hardware/drivers/encoders/ky040_driver.hpp"
#include <dirent.h>      // for opendir/readdir/closedir/struct dirent
#include <sys/stat.h>    // for stat() to check if directory

#include "hardware/drivers/psram_std/psram_std.hpp" //my custom work for psram stdd things
#include "hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"
// Encoder handles
static ky040_handle_t enc_left = nullptr;
static ky040_handle_t enc_right = nullptr;

// Tick counters (accumulated deltas)
static int32_t ticks_x = 0;  // left encoder
static int32_t ticks_y = 0;  // right encoder

#define LCD_BG_COLOR 0x0000  

extern const uint8_t avrclassic_font6x8[]; //hopefully pull font data
// --------------------- GLOBAL STATE ---------------------

uint16_t lcd_background_color= 0x0000;

spi_device_handle_t spi;   // <<< DEFINITION

static void on_encoder_tick(void* user_ctx, int delta) {
    // user_ctx is 0 for left, 1 for right
    uintptr_t which = (uintptr_t)user_ctx;

    if (which == 0) {
        ticks_x += delta;
        ESP_LOGI("ENC", "Left encoder: %ld", ticks_x);
    } else if (which == 1) {
        ticks_y += delta;
        ESP_LOGI("ENC", "Right encoder: %ld", ticks_y);
    }
}

void cpp_main(void){
	
    //initially worked by set driver, alloc fb, spi init dma, then lcd init

    // LCD setup (same as before)
    screen_set_driver(&onboard_screen_driver);
    framebuffer_alloc();
    //should be spi init dma here, not whatever is happening here
    lcd_init_simple();
    
    fb_clear(0x0000);  // black


fb_draw_text(4, 20, 80, "booting", 0xFFFF, 2,
             avrclassic_font6x8, 0, true, 0x0000, 40, {6,8});
lcd_fb_display_framebuffer(false, false);
//vTaskDelay(pdMS_TO_TICKS(1000));


//display_framebuffer(true, false);

// SD directory listing
DIR *dir = opendir("/sdcard");
if (dir == NULL) {
    ESP_LOGE("SD", "Failed to open /sdcard: %d (%s)", errno, strerror(errno));
    // Optional: show error on screen
  //  fb_text(10, 50, "SD open failed!", 0xF800, 2);  // red, size 2
    display_framebuffer(true, false);
    while(1) vTaskDelay(1000 / portTICK_PERIOD_MS);  // halt for debug
}

struct dirent *entry;

#define MAX_FILES 64
#define MAX_NAME_LEN 256

char file_list[MAX_FILES][MAX_NAME_LEN];

int file_count = 0;
int selected_idx = 0;
int scroll_offset = 0;

while ((entry = readdir(dir)) != NULL && file_count < MAX_FILES)
{
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;

    struct stat st;

    char fullpath[128];

    strlcpy(fullpath, "/sdcard/", sizeof(fullpath));
    strlcat(fullpath, entry->d_name, sizeof(fullpath));

    bool is_dir = (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode));

    snprintf(file_list[file_count], sizeof(file_list[file_count]),
             "%s%s", is_dir ? "[DIR] " : "      ", entry->d_name);

    file_count++;
}
closedir(dir);

ESP_LOGI("SD", "Found %d items in /sdcard", file_count);

// Window for file list (full screen)
auto win = std::make_shared<Window>(
    WindowCfg{
        .Posx = 0,
        .Posy = 0,
        .win_width = SCREEN_W-1,
        .win_height = SCREEN_H-1,
        .win_rotation = 0,
        .borderless = 0,
        .TextSizeMult = 1,
        .BgColor = 0xBBBB,
        .WinTextColor = 0xFFFF,
        .UpdateRate = 1.0f
    },
    "meowwy" 
);
fb_rect(0, 8, 25, 25, 100, 100, 0xFF34,0x5432);
display_framebuffer(true, false);

fb_draw_ptext(4, 40, 80, stdpsram::String("PSRAM LINK READY"), 0xFFFF, 2,
             avrclassic_font6x8, 0, true, 0x0000, 40, {6,8});
lcd_fb_display_framebuffer(false, false);
vTaskDelay(pdMS_TO_TICKS(2000));


win->Window::SetText(stdpsram::String("stupid ass test motherfucking string, i'm gonna throw my computer"));

win->WinDraw();

    display_framebuffer(true, false);


while (1) {
    // Poll encoders
    if (enc_left)  ky040_poll(enc_left);
    if (enc_right) ky040_poll(enc_right);

    bool needs_redraw = false;

    // Left encoder: select item
    if (ticks_x != 0) {
        selected_idx += ticks_x;
        ticks_x = 0;
        selected_idx = std::max(0, std::min(file_count - 1, selected_idx));
        needs_redraw = true;
    }

    // Right encoder: scroll
    if (ticks_y != 0) {
        scroll_offset += ticks_y * 3;
        ticks_y = 0;
        scroll_offset = std::max(0, std::min(file_count - 12, scroll_offset));
        needs_redraw = true;
    }

    if (!needs_redraw) {
        vTaskDelay(pdMS_TO_TICKS(30));
        continue;
    }

    // Build text buffer (fixed size – safe & fast)
    char text_buf[2048] = {0};  // 2 KB — plenty for ~15 lines + markup
    char *p = text_buf;

    // Title
    p += snprintf(p, text_buf + sizeof(text_buf) - p,
                  "<|size=2|><|b|>SD Card: /sdcard<|/b|><|/size|>\n\n");

    // Visible files
    int visible_lines = 0;
   stdpsram::String display_text;
display_text.reserve(2048);  // pre-allocate to avoid reallocs

display_text += "<|size=2|><|b|>SD Card: /sdcard<|/b|><|/size|>\n\n";

for (int i = scroll_offset; i < file_count && i < scroll_offset + 12; ++i) {
    if (i == selected_idx) {
        display_text += "<|hl=0xF800|><|b|>";
        display_text += file_list[i];
        display_text += "<|/b|><|/hl|>\n";
    } else {
        display_text += file_list[i];
        display_text += "\n";
    }
}

char status[128];
snprintf(status, sizeof(status),
         "\nItems: %d   Sel: %d/%d   <> select   ^v scroll",
         file_count, selected_idx + 1, file_count);

display_text += status;

win->SetText(display_text);  // now lives in PSRAM

    // Redraw
    win->WinDraw();

    display_framebuffer(true, false);

    vTaskDelay(pdMS_TO_TICKS(30));
}

}

//load entrypoint and boot
//note that the way the esp idf handles things is it starts off running c code but then loads main
// main is in cpp, so we have to extern this to c then load the c++


#define lcd_spi_host SPI3_HOST
#define sd_spi_host SPI2_HOST

extern "C" void app_main(void)
{
	
	//phase 1 set up simple hardware
	//  encoder init ---
    ky040_config_t cfg_left = {
        .clk_pin         = ENCODER0_CLK_PIN,
        .dt_pin          = ENCODER0_DT_PIN,
        .sw_pin          = ENCODER0_SW_PIN,
        .detents_per_rev = 20,
        .on_twist        = on_encoder_tick,
        .user_ctx        = (void*)(uintptr_t)0
    };
    esp_err_t err = ky040_new(&cfg_left, &enc_left);
    if (err != ESP_OK) ESP_LOGE("ENC", "Left failed: %s", esp_err_to_name(err));

    ky040_config_t cfg_right = {
        .clk_pin         = ENCODER1_CLK_PIN,
        .dt_pin          = ENCODER1_DT_PIN,
        .sw_pin          = ENCODER1_SW_PIN,
        .detents_per_rev = 20,
        .on_twist        = on_encoder_tick,
        .user_ctx        = (void*)(uintptr_t)1
    };
    err = ky040_new(&cfg_right, &enc_right);
    if (err != ESP_OK) ESP_LOGE("ENC", "Right failed: %s", esp_err_to_name(err));

    ESP_LOGI("ENC", "Encoders initialized");
	
	
	
	//phase 2 set up spi devices and scan for external ones
	
	
	
	//phase 3 set up spi
    // GPIO setup...
    gpio_config_t gpio_cfg = {};
    gpio_cfg.pin_bit_mask = (1ULL << LCD_DC) | (1ULL << LCD_RST) | (1ULL << lcd_BL);
    gpio_cfg.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&gpio_cfg));

    ESP_ERROR_CHECK(gpio_set_direction(SPI_CS_LCD, GPIO_MODE_OUTPUT));

    ESP_ERROR_CHECK(gpio_set_level(SPI_CS_LCD, 1));
    ESP_ERROR_CHECK(gpio_set_level(LCD_RST, 1));
    ESP_ERROR_CHECK(gpio_set_level(LCD_DC, 0));
    ESP_ERROR_CHECK(gpio_set_level(lcd_BL, 1));

    // LCD bus – zero-init + assignment
    spi_bus_config_t lcd_buscfg = {};
    lcd_buscfg.mosi_io_num = SPI_MOSI;
    lcd_buscfg.miso_io_num = -1;
    lcd_buscfg.sclk_io_num = SPI_CLK;
    lcd_buscfg.quadwp_io_num = -1;
    lcd_buscfg.quadhd_io_num = -1;
    lcd_buscfg.data4_io_num = -1;
    lcd_buscfg.data5_io_num = -1;
    lcd_buscfg.data6_io_num = -1;
    lcd_buscfg.data7_io_num = -1;
    lcd_buscfg.max_transfer_sz = SCREEN_W*SCREEN_H*2;// CHUNK_SIZE;// + 32;
    lcd_buscfg.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS;
    lcd_buscfg.data_io_default_level = 0;
    lcd_buscfg.isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO;
    lcd_buscfg.intr_flags = ESP_INTR_FLAG_IRAM;

    esp_err_t ret = spi_bus_initialize(lcd_spi_host, &lcd_buscfg, SPI_DMA_CH_AUTO);  //SPI_DMA_CH_AUTO seems to have it work, but disabled causes crashes
   
     if (ret != ESP_OK) {
		        ESP_LOGE("LCD", "SPI bus init failed: %s (0x%x)", esp_err_to_name(ret), ret);
        return;
    }
    ESP_LOGI("LCD", "SPI_HOST initialized OK");

    // LCD device
    spi_device_interface_config_t lcd_devcfg = {};
    lcd_devcfg.command_bits = 0;
    lcd_devcfg.address_bits = 0;
    lcd_devcfg.dummy_bits = 0;
    lcd_devcfg.mode = 0;
    lcd_devcfg.clock_source = SPI_CLK_SRC_DEFAULT;
    lcd_devcfg.duty_cycle_pos = 0;
    lcd_devcfg.cs_ena_pretrans = 0;
    lcd_devcfg.cs_ena_posttrans = 0;
    lcd_devcfg.clock_speed_hz = 76000000;
    lcd_devcfg.input_delay_ns = 0;
    lcd_devcfg.sample_point = SPI_SAMPLING_POINT_PHASE_0;
    lcd_devcfg.spics_io_num = SPI_CS_LCD;
    lcd_devcfg.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY;
    lcd_devcfg.queue_size = 3;
    lcd_devcfg.pre_cb = NULL;
    lcd_devcfg.post_cb = NULL;

    ret = spi_bus_add_device(lcd_spi_host, &lcd_devcfg, &spi);
    if (ret != ESP_OK) {
        ESP_LOGE("LCD", "SPI device add failed: %s (0x%x)", esp_err_to_name(ret), ret);
        return;
    }
    ESP_LOGI("LCD", "LCD SPI device added OK");
    
    
   

    // SD bus – same style
    spi_bus_config_t sd_buscfg = {};
    sd_buscfg.mosi_io_num = SD_MOSI;
    sd_buscfg.miso_io_num = SD_MISO;
    sd_buscfg.sclk_io_num = SD_SCK;
    sd_buscfg.quadwp_io_num = -1;
    sd_buscfg.quadhd_io_num = -1;
    sd_buscfg.data4_io_num = -1;
    sd_buscfg.data5_io_num = -1;
    sd_buscfg.data6_io_num = -1;
    sd_buscfg.data7_io_num = -1;
    sd_buscfg.max_transfer_sz = 8192;
    sd_buscfg.flags = SPICOMMON_BUSFLAG_MASTER;
    sd_buscfg.data_io_default_level = 0;
    sd_buscfg.isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO;
    sd_buscfg.intr_flags = 0;

    ret = spi_bus_initialize(sd_spi_host, &sd_buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE("SD", "SPI bus init failed: %s (0x%x)", esp_err_to_name(ret), ret);
    } else {
        ESP_LOGI("SD", "SPI_HOST initialized OK");
    }

    // SD mount code remains the same...
    sdspi_device_config_t sd_slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    sd_slot_config.host_id = sd_spi_host;
    sd_slot_config.gpio_cs = SPI_CS_SD;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    sdmmc_card_t *card = NULL;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &sd_slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE("SD", "Mount failed: %s (0x%x)", esp_err_to_name(ret), ret);
    } else {
        ESP_LOGI("SD", "SD card mounted successfully /n info");
        sdmmc_card_print_info(stdout, card);
    }

    cpp_main();
}