// =============================================================================
// Drop-in structure for bootloader_final_app() — role-aware final stage
// =============================================================================
// Copy the body into your existing function. Requires:
//   #include "boot_role.hpp"
//   #include "mwenv_dom.hpp"          // puppet TX / tyrant RX later
//   #include "rs_dom_link.hpp"
// =============================================================================

#include "boot_role.hpp"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Forward decls you already have
extern void lcd_init_simple();
extern void refreshScreen();
extern void fb_clear(uint16_t);
extern void fb_draw_text(...);  // match your signature
extern void launchTHESTUPIDMOTHERFUCKINGPEICEOFSHITDISPLAYPUSHTASKFUCKYOU();
extern void core1_createData(void*);
extern TaskHandle_t core1TaskHandle;
extern SemaphoreHandle_t g_display_mutex;

extern void register_watch();
extern void register_menu();
extern void register_fileviewer();
extern void register_pong();
extern void register_snake();
extern void register_2048();
extern void register_browser();

// Optional: start UART DOM link (implement in rs_dom_uart.cpp later)
extern "C" void rs_dom_link_start_tx(void);  // puppet
extern "C" void rs_dom_link_start_rx(void);  // tyrant

static const char* TAG = "boot_final";

static void bootloader_final_app() {
    vTaskDelay(pdMS_TO_TICKS(50));

    const boot_role_t role = boot_role_resolve();
    // Mirror into env so apps can branch without including boot_role.h
    v_env.headless = !boot_has_display(role);

    ESP_LOGI(TAG, "=== final boot role: %s (headless=%d) ===",
             boot_role_name(role), (int)v_env.headless);

    // -------------------------------------------------------------------------
    // Display stack — only if this node owns a panel
    // -------------------------------------------------------------------------
    if (boot_has_display(role)) {
        screen_set_driver(&onboard_screen_driver);
        vTaskDelay(pdMS_TO_TICKS(50));
        ESP_LOGI(TAG, "LCD init");
        lcd_init_simple();
        vTaskDelay(pdMS_TO_TICKS(100));
        fb_clear(0x0000);
        fb_draw_text(4, 20, 80, "booting", 0xFFFF, 2,
                     0, true, 0x0000, 40, ft_AVR_classic_6x8);
        vTaskDelay(pdMS_TO_TICKS(50));
        refreshScreen();
        vTaskDelay(pdMS_TO_TICKS(50));
        fb_clear(0x0000);

        auto& wm = WindowManager::getInstance();
        (void)wm;
        ESP_LOGI(TAG, "WindowManager ready");

        g_display_mutex = xSemaphoreCreateMutex();
        launchTHESTUPIDMOTHERFUCKINGPEICEOFSHITDISPLAYPUSHTASKFUCKYOU();
    } else {
        ESP_LOGI(TAG, "headless: skip LCD / WindowManager display path");
        // Still construct WM if apps create Windows for DOM packing —
        // windows exist as data, WinDraw is simply never called / no-ops
        // when headless (see MWenv WinDraw early-out on headless if you add it).
        (void)WindowManager::getInstance();
    }

    // -------------------------------------------------------------------------
    // WDT + shared services
    // -------------------------------------------------------------------------
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 10000,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true
    };
    esp_task_wdt_reconfigure(&wdt_config);

    // core1 data task — useful on both roles
    xTaskCreatePinnedToCore(core1_createData, "core1", 8192, NULL, 5, &core1TaskHandle, 1);

    // -------------------------------------------------------------------------
    // UART collaborative link
    // -------------------------------------------------------------------------
    if (boot_sends_dom(role)) {
        ESP_LOGI(TAG, "starting DOM TX (puppet → tyrant)");
        rs_dom_link_start_tx();
    }
    if (boot_receives_dom(role)) {
        ESP_LOGI(TAG, "starting DOM RX (tyrant ← puppets)");
        rs_dom_link_start_rx();
    }

    // -------------------------------------------------------------------------
    // App manager + registrations
    // -------------------------------------------------------------------------
    auto& manager = appManager::instance();
    manager.start_manager_task();
    vTaskDelay(pdMS_TO_TICKS(8));

    v_env.CurrentHIDTarget = (HIDTarget)HIDTarget::toTaskAndDebug;

    // Always register factories — cheap; open only what the role needs
    register_watch();
    register_menu();
    register_fileviewer();
    register_pong();
    register_snake();
    register_2048();
    register_browser();

    if (boot_runs_apps_locally(role)) {
        // SOLO + PUPPET: real app tasks. Puppet apps mark windows dirty;
        // a small DomEmit task packs and UART-sends each frame.
        auto watchapp = manager.open_app("WatchApp");
        if (!watchapp) {
            ESP_LOGE(TAG, "Failed to launch WatchApp");
        }
    } else {
        // TYRANT: no local game/watch — display is driven by incoming DomFrames.
        // Optional: open a minimal "DomViewer" app that owns one fullscreen window
        // and applies RX frames into it.
        ESP_LOGI(TAG, "tyrant: waiting for puppet DOM (no local WatchApp)");
        // manager.open_app("DomViewerApp");  // when you add it
    }

    vTaskDelay(pdMS_TO_TICKS(8));
    vTaskDelete(NULL);
}
