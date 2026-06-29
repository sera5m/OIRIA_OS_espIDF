/*
 * bootfunctions.hpp
 * 
 * Boot stage functions extracted from main.
 * Header guards + prototypes.
 */

#pragma once

#ifndef BOOTFUNCTIONS_H_
#define BOOTFUNCTIONS_H_

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"


extern sdmmc_card_t *card;
// ────────────────────────────────────────────────
// Prototypes
// ────────────────────────────────────────────────


esp_err_t stage_1_encoders(void);            
esp_err_t stage_2_i2c_scan(void);            
esp_err_t boot_stage2andaHalf(void);         
esp_err_t stage_3_spi_set(bool enable);      
esp_err_t stage_3_sd_mount(void);            
// New de-init / utility functions
esp_err_t stage_3_spi_deinit(void);
esp_err_t sd_unmount(void);
esp_err_t sd_remount(void);
esp_err_t lcd_deinit(void);
esp_err_t lcd_reinit(void);

#endif // BOOTFUNCTIONS_H_