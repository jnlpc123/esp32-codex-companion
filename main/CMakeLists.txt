idf_component_register(SRCS
    "main.c" "display/backlight.c" "display/display_init.c"
    "display/animation_cache.c" "display/animation_crc.c" "display/companion_render.c" "display/alpha_cache.c"
    "../tests/companion/alpha_cache_selftest.c"
    "settings/settings.c" "storage/mount.c" "storage/fast_read.c"
    "core/app_tasks.c" "companion/companion.c" "companion/task_status.c" "companion/usb_link.c" "companion/notify_tone.c"
    "audio/es8311.c" "wifi/wifi_manager.c" "wifi/captive_portal.c" "dns_server/dns_server.c"
    "file_server/file_server.c" "file_server/import_transaction.c" "file_server/file_server_wifi.c" "file_server/file_server_sysinfo.c"
    INCLUDE_DIRS "." "core" "display" "wifi" "file_server" "settings" "storage" "audio" "dns_server" "companion"
    PRIV_REQUIRES esp_app_format esp_hw_support spi_flash esp_partition esp_lcd_gc9a01 esp_lcd_panel_io_additions
    esp_lcd esp_timer esp_rom fatfs esp_event vfs esp_netif nvs_flash spiffs esp_http_server esp_jpeg
    esp_driver_i2c esp_driver_i2s esp_driver_ledc esp_driver_usb_serial_jtag esp_wifi lwip driver esp_codec_dev mdns cjson)

target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_partition_read" "-Wl,--wrap=SPIFFS_gc")
foreach(symbol SPIFFS_open SPIFFS_write SPIFFS_close SPIFFS_fflush SPIFFS_remove SPIFFS_rename SPIFFS_update_meta SPIFFS_fupdate_meta SPIFFS_ftruncate spiffs_gc_check)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${symbol}")
endforeach()
target_include_directories(${COMPONENT_LIB} PRIVATE "$ENV{IDF_PATH}/components/spiffs" "$ENV{IDF_PATH}/components/spiffs/spiffs/src")
set_source_files_properties("display/animation_cache.c" "display/animation_crc.c" "display/companion_render.c" "display/alpha_cache.c" PROPERTIES COMPILE_OPTIONS "-O3")
idf_component_get_property(animation_spiffs_lib spiffs COMPONENT_LIB)
target_compile_options(${animation_spiffs_lib} PRIVATE -O3)

# Read-only UI travels with the app. Existing SPIFFS user media is never replaced.
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/../web/index.html" BINARY RENAME_TO vibe_index)
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/../web/companion.css" BINARY RENAME_TO vibe_css)
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/../web/companion.js" BINARY RENAME_TO vibe_js)
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/../web/vendor/gifuct.js" BINARY RENAME_TO vibe_gif)
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/../web/alpha-codec.js" BINARY RENAME_TO vibe_alpha)
