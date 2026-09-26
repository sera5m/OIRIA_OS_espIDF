# ESP-IDF main component. Firmware sources live in the repo root (parent of this file).
# Do not nest this repo inside another example's main/ — clone and idf.py build from the root.

idf_component_register(
    SRCS
        "../tusb_hid_example_main.cpp"
        "../os_code/core/com/rs_autoconnect.cpp"
        "../code_stuff/helperfunctions.cpp"
        "../code_stuff/types.c"
        "../hardware/drivers/devices_i2c/i2cHelpers.c"
        "../os_code/applications/discord/discord_rpc.cpp"
        "../hardware/drivers/lcd/fonts/font_basic_types.c"
        "../hardware/drivers/abstraction_layers/al_scr.c"
        "../hardware/drivers/lcd/st7789v2/lcDriver.c"
        "../hardware/drivers/lcd/st7789v2/lcdriverAddon.cpp"
        "../hardware/drivers/lcd/fonts/font_avr_classics.c"
        "../hardware/drivers/encoders/ky040_driver.cpp"
        "../hardware/drivers/generic/button_driver.cpp"
        "../os_code/middle_layer/input/input_handler.cpp"
        "../os_code/middle_layer/input/inputProscessorTask/ipt_x.cpp"
        "../hardware/drivers/sd_card/d_sdc.c"
        "../os_code/core/rShell/enviroment/env_vars.c"
        "../os_code/core/window_env/MWenv.cpp"
        "../os_code/core/window_env/Canvas.cpp"
        "../os_code/core/window_env/PsramBackgroundTile.cpp"
        "../os_code/core/window_env/AnimWorld.cpp"
        "../os_code/core/rShell/rshell_appFramework.cpp"
        "../os_code/core/rShell/rshell_appmanager.cpp"
        "../os_code/core/rs_vm/vm/rs_vm.cpp"
        "../os_code/core/rs_vm/vm/rs_vm_parse.cpp"
        "../os_code/core/rs_vm/vm/rs_vm_nseq.cpp"
        "../os_code/core/rs_vm/vm/rs_vm_host_esp.cpp"
        "../os_code/core/rs_vm/vm/rs_vm_host_siggen.cpp"
        "../os_code/core/rs_vm/debugger/rs_dbg.cpp"
        "../os_code/core/rs_vm/vm_modules/vm_mdl_immut/vm_mdl_immut.cpp"
        "../os_code/core/rs_vm/vm_modules/vm_mdl_thread/vm_mdl_thread.cpp"
        "../os_code/core/rs_vm/vm_modules/vm_mdl_property/vm_mdl_property.cpp"
        "../os_code/core/rs_vm/vm_modules/vm_mdl_flowmap/vm_mdl_flowmap.cpp"
        "../os_code/core/rs_vm/vm_modules/vm_mdl_shell/vm_mdl_shell.cpp"
        "../os_code/applications/vulcanApp/MS_vulcanapp.cpp"
        "../os_code/core/rShell/streams/rshell_pool.cpp"
        "../os_code/core/rShell/streams/rshell_pipe.cpp"
        "../os_code/core/rShell/streams/rshell_streamdefs.c"
        "../os_code/core/rShell/streams/rshell_pump.cpp"
        "../os_code/core/com/boot_role.c"
        "../os_code/core/com/rs_collective_uart.c"
        "../os_code/core/com/rs_vulcan_httpd.cpp"
        "../os_code/core/window_env/rs_dom_link.cpp"
        "../os_code/core/window_env/mwenv_dom.cpp"
        "../os_code/core/notification_sys/rs_notif_dispatcher.c"
        "../Fboot/bootfunctions.cpp"
        "../sss/shared_state.c"
        "../os_code/applications/watch/MS_watchapp.cpp"
        "../os_code/applications/menu/app_menu.cpp"
        "../os_code/applications/fileviewer/MS_file_viewer.cpp"
        "../os_code/applications/browser/html_to_mwenv.cpp"
        "../os_code/applications/browser/MS_browserapp.cpp"
        "../os_code/applications/pong/MS_pongapp.cpp"
        "../os_code/applications/snake/MS_snakeapp.cpp"
        "../os_code/applications/2048/MS_2048app.cpp"
        "../os_code/applications/siggen/MS_siggenapp.cpp"
        "../os_code/applications/scope/MS_scopeapp.cpp"
        "../code_stuff/signal_gen/siggen_wavetable.cpp"
        "../code_stuff/signal_gen/siggen_ledc.cpp"
        "../code_stuff/signal_gen/siggen_i2s_pdm.cpp"
        "../code_stuff/signal_gen/siggen_ctrl.cpp"
        "../code_stuff/signal_gen/siggen_adc_cal.cpp"
        "../code_stuff/signal_gen/sig_protocol.cpp"
        "../code_stuff/signal_gen/sig_insn.cpp"
        "../code_stuff/signal_gen/siggen_play.cpp"
        EMBED_TXTFILES
        "../os_code/core/com/autoconnect.conf"
        "../os.conf"
    INCLUDE_DIRS
        "../."
        "../os_code/core/com"
        "../code_stuff"
        "../code_stuff/signal_gen"
        "../code_stuff/precomputed_math"
        "../hardware/drivers/devices_i2c"
        "../hardware/drivers/lcd/fonts"
        "../hardware/drivers/lcd/st7789v2"
        "../hardware/drivers/abstraction_layers"
        "../hardware/drivers/encoders"
        "../hardware/drivers/generic"
        "../os_code/middle_layer/input"
        "../os_code/middle_layer/input/inputProscessorTask"
        "../os_code/core/rShell"
        "../os_code/core/rShell/enviroment"
        "../os_code/core/window_env"
        "../os_code/core/rs_vm"
        "../os_code/core/rs_vm/vm"
        "../os_code/applications/vulcanApp"
        "../os_code/applications/watch"
        "../hardware/wiring"
        "../hardware/drivers/psram_std"
        "../os_code/core/notification_sys"
        "../Fboot"
        "../sss"
    PRIV_REQUIRES
        nvs_flash
        esp_driver_gpio
        driver
        log
        esp_netif
        esp_wifi
        esp_event
        fatfs
        sdmmc
        freertos
        tinyusb
        esp_http_server
        esp_psram
        esp_timer
        esp_driver_i2c
        esp_http_client
        esp_adc
    REQUIRES
        ulp
        esp_driver_pcnt
        esp_timer
        freertos
        driver
        vfs
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE RSVM_IN_FIRMWARE=1)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused
    -fexceptions
    -Wno-missing-field-initializers
    -Wno-error=missing-field-initializers
    -Wno-format-truncation
    -Wno-error=reorder
    -Wno-error=array-compare
)

target_add_binary_data(${COMPONENT_LIB} "../os.conf" TEXT)

# sdkconfig currently has CONFIG_ULP_COPROC_ENABLED off. Embedding anyway
# makes IDF compile ulp_main.c as FSM ULP with the Xtensa compiler, which
# cannot find ulp_riscv.h. Only embed when RISC-V ULP is actually on.
if(CONFIG_ULP_COPROC_ENABLED)
    set(ulp_app_name ulp_main)
    set(ulp_sources "../ulp/ulp_main.c")
    set(ulp_exp_dep_srcs
        "../tusb_hid_example_main.cpp"
        "../sss/shared_state.h"
        "../sss/shared_state.c"
        "../ulp/ulp_main.h"
    )
    ulp_embed_binary(${ulp_app_name} "${ulp_sources}" "${ulp_exp_dep_srcs}")
endif()
