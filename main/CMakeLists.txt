idf_component_register(SRCS
    "main.c" "display/backlight.c" "display/display_init.c"
    "display/animation_cache.c" "display/animation_crc.c" "display/companion_render.c" "display/alpha_cache.c" "display/jpeg_decode.c"
    "../tests/companion/alpha_cache_selftest.c"
    "settings/settings.c" "settings/factory_reset.c" "storage/mount.c" "storage/fast_read.c" "storage/media_storage.c" "storage/storage_service.c"
    "core/app_tasks.c" "companion/companion.c" "companion/task_status.c" "companion/usb_link.c" "companion/notify_tone.c"
    "audio/es8311.c" "wifi/wifi_manager.c" "wifi/wifi_provision.c" "wifi/wifi_credentials.c" "wifi/wifi_control.c" "wifi/wifi_prompt_assets.c" "wifi/captive_portal.c" "dns_server/dns_server.c"
    "file_server/file_server.c" "file_server/import_transaction.c" "file_server/file_server_wifi.c" "file_server/file_server_sysinfo.c"
    INCLUDE_DIRS "." "core" "display" "wifi" "file_server" "settings" "storage" "audio" "dns_server" "companion"
    PRIV_REQUIRES esp_app_format esp_hw_support spi_flash esp_partition esp_lcd_gc9a01 esp_lcd_panel_io_additions
    esp_lcd esp_timer esp_rom fatfs esp_event vfs esp_netif nvs_flash spiffs esp_http_server esp_jpeg
    esp_driver_i2c esp_driver_i2s esp_driver_ledc esp_driver_usb_serial_jtag esp_wifi lwip driver esp_codec_dev mdns cjson mbedtls)

target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_partition_read" "-Wl,--wrap=SPIFFS_gc")
foreach(symbol SPIFFS_open SPIFFS_write SPIFFS_lseek SPIFFS_close SPIFFS_fflush SPIFFS_remove SPIFFS_rename SPIFFS_update_meta SPIFFS_fupdate_meta SPIFFS_ftruncate spiffs_gc_check)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${symbol}")
endforeach()
target_include_directories(${COMPONENT_LIB} PRIVATE "$ENV{IDF_PATH}/components/spiffs" "$ENV{IDF_PATH}/components/spiffs/spiffs/src")
set_source_files_properties("display/animation_cache.c" "display/animation_crc.c" "display/companion_render.c" "display/alpha_cache.c" PROPERTIES COMPILE_OPTIONS "-O3")
idf_component_get_property(animation_spiffs_lib spiffs COMPONENT_LIB)
target_compile_options(${animation_spiffs_lib} PRIVATE -O3)

# Read-only UI travels with the app. Existing SPIFFS user media is never replaced.
set(web_dir "${CMAKE_CURRENT_SOURCE_DIR}/../web")
set(web_gzip "${CMAKE_CURRENT_BINARY_DIR}/web_gzip")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${web_dir}/index.html" "${web_dir}/companion.css" "${web_dir}/companion.js"
    "${web_dir}/wifi.html" "${web_dir}/wifi.js" "${web_dir}/network.js"
    "${web_dir}/vendor/gifuct.js" "${web_dir}/alpha-codec.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools/compress_web.py")
execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/compress_web.py" "${web_dir}" "${web_gzip}"
    RESULT_VARIABLE web_gzip_result)
if(NOT web_gzip_result EQUAL 0)
    message(FATAL_ERROR "Failed to compress embedded web assets")
endif()
target_add_binary_data(${COMPONENT_LIB} "${web_gzip}/vibe_index.gz" BINARY RENAME_TO vibe_index)
target_add_binary_data(${COMPONENT_LIB} "${web_gzip}/vibe_css.gz" BINARY RENAME_TO vibe_css)
target_add_binary_data(${COMPONENT_LIB} "${web_gzip}/vibe_js.gz" BINARY RENAME_TO vibe_js)
target_add_binary_data(${COMPONENT_LIB} "${web_gzip}/vibe_gif.gz" BINARY RENAME_TO vibe_gif)
target_add_binary_data(${COMPONENT_LIB} "${web_gzip}/vibe_alpha.gz" BINARY RENAME_TO vibe_alpha)

# Original author provisioning images live in app flash, never user SPIFFS.
target_add_binary_data(${COMPONENT_LIB} "wifi/assets/ap-mode.jpg" BINARY RENAME_TO wifi_ap)
target_add_binary_data(${COMPONENT_LIB} "wifi/assets/wifi-connecting.jpg" BINARY RENAME_TO wifi_connecting)
target_add_binary_data(${COMPONENT_LIB} "wifi/assets/wifi-connected.jpg" BINARY RENAME_TO wifi_connected)

target_add_binary_data(${COMPONENT_LIB} "${web_gzip}/vibe_wifi.gz" BINARY RENAME_TO vibe_wifi)
target_add_binary_data(${COMPONENT_LIB} "${web_gzip}/vibe_wifi_js.gz" BINARY RENAME_TO vibe_wifi_js)
target_add_binary_data(${COMPONENT_LIB} "${web_gzip}/vibe_network_js.gz" BINARY RENAME_TO vibe_network_js)
