# cmake_minimum_required(VERSION 3.16)
# include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# project(esp32_hid_web)

idf_component_register(
    SRCS "tusb_hid_example_main.c"
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
