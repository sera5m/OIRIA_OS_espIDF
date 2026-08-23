
//info: this file is part of bootloader. 
//originally in the application bootloader final app


#define App_reg_table \
    register_watch(); \
    register_menu(); \
    register_fileviewer(); \
    register_pong(); \
    register_vulcan(); \
    register_snake(); \
    register_2048(); \
    register_browser();

#define Register_appTable() \
    do { \
        ESP_LOGE(TAG, "registering application table from app_registerTable.h"); \
        App_reg_table \
    } while(0)
