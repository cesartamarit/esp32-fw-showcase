idf_component_register(
    SRCS "menu_mgr.c"
    INCLUDE_DIRS "include"
    REQUIRES display encoder bsp power_mgr esp_app_format freertos
)
