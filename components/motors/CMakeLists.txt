idf_component_register(
    SRCS "motors.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_ledc esp_driver_gpio freertos
)

# Compile this component at DEBUG log level so ESP_LOGD prints are included.
# The global log level stays at INFO for everything else.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
