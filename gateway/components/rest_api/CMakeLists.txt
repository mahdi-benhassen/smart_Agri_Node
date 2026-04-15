idf_component_register(SRCS "rest_api.c" INCLUDE_DIRS "."
    REQUIRES esp_http_server json agri_data_model zigbee_coordinator system_monitor)
