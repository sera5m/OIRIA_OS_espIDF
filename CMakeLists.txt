# cmake_minimum_required(VERSION 3.16)
# include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# project(esp32_hid_web)

idf_component_register(
    SRCS
     "tusb_hid_example_main.cpp"
     
     
     #bitch bullshit
     "code_stuff/helperfunctions.cpp"
     "code_stuff/helperfunctions.hpp"
     "code_stuff/types.c"
     "code_stuff/types.h"
     #drivers
     #lcd drivers
       "hardware/drivers/lcd/fonts/font_basic_types.h"
        "hardware/drivers/lcd/fonts/font_basic_types.c"
       
  "hardware/drivers/lcd/st7789v2/lcDriver.c"
    "hardware/drivers/lcd/st7789v2/lcDriver.h"
    "hardware/drivers/lcd/fonts/font_avr_classics.h"
    "hardware/drivers/lcd/fonts/font_avr_classics.c"
  
    #os code

    
    #enviroment----warning not tested
    
   "os_code/core/window_env/MWenv.cpp"
    "os_code/core/window_env/MWenv.hpp"
    
    INCLUDE_DIRS "."
    PRIV_REQUIRES
	nvs_flash
        esp_driver_gpio
        driver
        log
        esp_netif
        esp_wifi
        freertos
        tinyusb
        esp_http_server
        esp_psram
        esp_timer
        
        
)

# Switch C standard if needed
# set(CMAKE_C_STANDARD 99)   # or 17
# set(CMAKE_C_STANDARD_REQUIRED ON)
