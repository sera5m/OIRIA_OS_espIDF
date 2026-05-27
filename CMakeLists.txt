idf_component_register(
    SRCS
        # Main
        "tusb_hid_example_main.cpp"
        
        # Helper code
        "code_stuff/helperfunctions.cpp"
        "code_stuff/types.c"
        
        # I2C drivers
        "hardware/drivers/devices_i2c/i2cHelpers.c"
        
        # LCD drivers
        "hardware/drivers/lcd/fonts/font_basic_types.c"
        "hardware/drivers/abstraction_layers/al_scr.c"
        "hardware/drivers/lcd/st7789v2/lcDriver.c"
        "hardware/drivers/lcd/st7789v2/lcdriverAddon.cpp"
        "hardware/drivers/lcd/fonts/font_avr_classics.c"
        
        # Input handling
        "hardware/drivers/encoders/ky040_driver.cpp"
        "hardware/drivers/generic/button_driver.cpp"
        "os_code/middle_layer/input/input_handler.cpp"
        "os_code/middle_layer/input/inputProscessorTask/ipt_x.cpp"
        
        #storage 
        "hardware/drivers/sd_card/d_sdc.c" #this isn't an app! it's a driver!
        
        #program loading and storage
        "os_code/core/rShell/defaultAppList.hpp" 
        
        
        # OS code
        "os_code/core/rShell/enviroment/env_vars.c"
        "os_code/core/window_env/MWenv.cpp"
        "os_code/core/rShell/s_hell.cpp"
        
        
        # Applications
        "os_code/applications/watch/MS_watchapp.cpp"
        #"os_code/applications/fileviewwer/MS_file_viewwer.cpp"
        "os_code/applications/menu/app_menu.cpp"
        
    
    INCLUDE_DIRS 
        "."
        "code_stuff"
        "hardware/drivers/devices_i2c"
        "hardware/drivers/lcd/fonts"
        "hardware/drivers/lcd/st7789v2"
        "hardware/drivers/abstraction_layers"
        "hardware/drivers/encoders"
        "hardware/drivers/generic"
        "os_code/middle_layer/input"
        "os_code/middle_layer/input/inputProscessorTask"
        "os_code/core/rShell"
        "os_code/core/rShell/enviroment"
        "os_code/core/window_env"
        "os_code/applications/watch"
        "hardware/wiring"
        "hardware/drivers/psram_std"
    
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
        esp_driver_i2c
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused -fexceptions -Wno-missing-field-initializers -Wno-error=missing-field-initializers -Wno-format-truncation)