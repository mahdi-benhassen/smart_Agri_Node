idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash agri_data_model sensor_hub actuator_controller
             zigbee_end_device power_manager nvs_config ota_node
             rain_gauge_driver
)
