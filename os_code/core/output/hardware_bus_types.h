// hardware_has_bus.h
// Sorted by category. Covers every major peripheral/bus on ESP32-S3.
// Note: SPI0/SPI1 are reserved for flash/PSRAM (not general-purpose).
// No DAC on S3 (use LEDC PWM or I2S PDM instead).
//NOTE: THIS WAS GENERATED WITH AI, SO IT MAY NOT BE 100% ACCURATE. PLEASE VERIFY AGAINST ESP32-S3 TECHNICAL REFERENCE MANUAL IF YOU ARE HAVING KERNEL BUGS
typedef enum {
    // ========== Serial / Communication Buses ==========
    HARDWARE_BUS_SPI2,          // General-purpose SPI (FSPI)
    HARDWARE_BUS_SPI3,          // General-purpose SPI
    HARDWARE_BUS_I2C0,          // I2C master/slave
    HARDWARE_BUS_I2C1,          // I2C master/slave
    HARDWARE_BUS_UART0,         // UART (often console / USB-CDC)
    HARDWARE_BUS_UART1,         // UART
    HARDWARE_BUS_UART2,         // UART
    HARDWARE_BUS_I2S0,          // I2S audio / PDM
    HARDWARE_BUS_I2S1,          // I2S audio / PDM
    HARDWARE_BUS_TWAI,          // CAN 2.0 (TWAI)
    HARDWARE_BUS_USB_OTG,       // USB 2.0 Full-Speed OTG
    HARDWARE_BUS_USB_SERIAL_JTAG,// USB Serial/JTAG controller
    HARDWARE_BUS_SDMMC,         // SD/MMC host (2 slots)
    HARDWARE_BUS_LCD_CAM,       // Parallel LCD + Camera (DVP 8-16 bit)

    // ========== PWM / Timing / Control ==========
    HARDWARE_BUS_LEDC,          // LED PWM (8 channels)
    HARDWARE_BUS_MCPWM0,        // Motor Control PWM
    HARDWARE_BUS_MCPWM1,        // Motor Control PWM
    HARDWARE_BUS_RMT,           // Remote Control (TX/RX, IR, WS2812, etc.)
    HARDWARE_BUS_PCNT,          // Pulse Counter
    HARDWARE_BUS_TIMER_GROUP0,  // General-purpose timers (2x 54-bit)
    HARDWARE_BUS_TIMER_GROUP1,  // General-purpose timers (2x 54-bit)
    HARDWARE_BUS_SYSTIMER,      // System timer (52-bit)
    HARDWARE_BUS_WDT,           // Watchdog timers

    // ========== Analog / Sensing ==========
    HARDWARE_BUS_ADC1,          // 12-bit SAR ADC (10 channels)
    HARDWARE_BUS_ADC2,          // 12-bit SAR ADC (10 channels, conflicts with Wi-Fi)
    HARDWARE_BUS_TOUCH,         // Capacitive touch (14 channels)
    HARDWARE_BUS_TEMP_SENSOR,   // On-chip temperature sensor

    // ========== DMA / System ==========
    HARDWARE_BUS_GDMA,          // General DMA (5 TX + 5 RX channels)
    HARDWARE_BUS_GPIO,          // GPIO matrix / IO MUX
    HARDWARE_BUS_RTC_GPIO,      // RTC domain GPIO + ULP

    // ========== Crypto / Security (hardware accelerators) ==========
    HARDWARE_BUS_AES,
    HARDWARE_BUS_SHA,
    HARDWARE_BUS_RSA,
    HARDWARE_BUS_HMAC,
    HARDWARE_BUS_DS,            // Digital Signature
    HARDWARE_BUS_RNG,           // True RNG
    HARDWARE_BUS_XTS_AES,       // Flash/PSRAM encryption

    HARDWARE_BUS_COUNT          // Sentinel
} hardware_has_bus_t;