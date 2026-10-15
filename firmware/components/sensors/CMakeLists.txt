idf_component_register(
    SRCS 
        "MPU6050.cpp"
        "TPiS1385.cpp"
    INCLUDE_DIRS "." "${PROJECT_DIR}/main"
    REQUIRES esp_driver_i2c esp_driver_gpio
)
