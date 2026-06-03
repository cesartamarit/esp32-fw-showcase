idf_component_register(
    SRCS "ble_stack.c"
    INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash bsp power_mgr ota_manager
)
