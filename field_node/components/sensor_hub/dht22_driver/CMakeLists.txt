idf_component_register(
    SRCS "dht22_driver.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer
)
