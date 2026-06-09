idf_component_register(
    SRCS "ota_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_https_ota app_update nvs_flash wifi_mgr mbedtls power_mgr esp_coex
)
