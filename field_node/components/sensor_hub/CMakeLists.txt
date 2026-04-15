idf_component_register(
    SRCS "sensor_hub.c"
         "dht22_driver/dht22_driver.c"
         "sht40_driver/sht40_driver.c"
         "soil_moisture_driver/soil_moisture_driver.c"
         "npk_rs485_driver/npk_rs485_driver.c"
         "rain_gauge_driver/rain_gauge_driver.c"
         "bh1750_driver/bh1750_driver.c"
         "scd40_driver/scd40_driver.c"
    INCLUDE_DIRS "."
                 "dht22_driver"
                 "sht40_driver"
                 "soil_moisture_driver"
                 "npk_rs485_driver"
                 "rain_gauge_driver"
                 "bh1750_driver"
                 "scd40_driver"
    REQUIRES driver esp_adc esp_timer agri_data_model
)
