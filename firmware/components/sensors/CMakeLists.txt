idf_component_register(
    SRCS
        "I2CDevice.cpp"
        "MPU6050.cpp"
        "TPiS1385.cpp"
        "MAX30102.cpp"
        "GroveGSR.cpp"
    INCLUDE_DIRS "." "${PROJECT_DIR}/main"
    PRIV_REQUIRES esp_timer
    REQUIRES esp_driver_i2c esp_driver_gpio esp_adc ppg_processing
)
