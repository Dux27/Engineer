idf_component_register(
    SRCS
        "I2CDevice.cpp"
        "MPU6050.cpp"
        "TPiS1385.cpp"
        "MAX30105.cpp"
    INCLUDE_DIRS "." "${PROJECT_DIR}/main"
    PRIV_REQUIRES esp_timer
    REQUIRES esp_driver_i2c esp_driver_gpio ppg_processing
)
