idf_component_register(
    SRCS "motors.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_ledc esp_driver_gpio freertos
)
