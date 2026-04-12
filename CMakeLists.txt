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
     
     "hardware/drivers/devices_i2c/i2cHelpers.c"
      "hardware/drivers/devices_i2c/i2cHelpers.c"
      
      
     #more extensions for this device
     "hardware/drivers/psram_std/psram_std.hpp"
    			 #lcd drivers
      			 "hardware/drivers/lcd/fonts/font_basic_types.h"
      			  "hardware/drivers/lcd/fonts/font_basic_types.c"
        
      			  "hardware/drivers/abstraction_layers/al_scr.h"
       			 "hardware/drivers/abstraction_layers/al_scr.c"
       
       
       							#screen drivers
 				 "hardware/drivers/lcd/st7789v2/lcDriver.c"
    							"hardware/drivers/lcd/st7789v2/lcDriver.h"
    							
    				"hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"		
    				"hardware/drivers/lcd/st7789v2/lcdriverAddon.cpp"		
    		#font
    				"hardware/drivers/lcd/fonts/font_avr_classics.h"
   				 "hardware/drivers/lcd/fonts/font_avr_classics.c"
   				 
   				 
   	#handle input		 
   			"hardware/drivers/encoders/ky040_driver.cpp"
   			"hardware/drivers/encoders/ky040_driver.hpp"
   			"os_code/middle_layer/input/input_devs_agg.hpp"
   			"os_code/middle_layer/input/input_handler.cpp"
  			"os_code/middle_layer/input/input_handler.hpp"
  			#"os_code/middle_layer/input/hostusb/usb_hid_host.cpp"
  			#"os_code/middle_layer/input/hostusb/usb_hid_host.hpp"
  			"os_code/middle_layer/input/inputProscessorTask/ipt_x.hpp"
  			"os_code/middle_layer/input/inputProscessorTask/ipt_x.cpp"
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
        esp_driver_i2c
        
)
#set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components/EspUsbHost")
# yes this is cloned from github https://github.com/tanakamasayuki/EspUsbHost.git
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused -fexceptions -Wno-missing-field-initializers -Wno-error=missing-field-initializers -Wno-format-truncation)
# Switch C standard if needed

# set(CMAKE_C_STANDARD 99)   # or 17
# set(CMAKE_C_STANDARD_REQUIRED ON)
