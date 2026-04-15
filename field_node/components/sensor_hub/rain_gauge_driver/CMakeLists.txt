idf_component_register(
    SRCS "rain_gauge_driver.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer
)
