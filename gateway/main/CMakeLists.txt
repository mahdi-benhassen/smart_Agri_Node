idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_netif esp_event agri_data_model
             nvs_config system_monitor sntp_sync wifi_manager
             zigbee_coordinator mqtt_client rest_api cloud_buffer ota_manager
)
