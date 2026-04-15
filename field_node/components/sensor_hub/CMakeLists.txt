idf_component_register(
    SRCS "sensor_hub.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_adc esp_timer agri_data_model
             dht22_driver sht40_driver soil_moisture_driver
             npk_rs485_driver rain_gauge_driver bh1750_driver scd40_driver
)
