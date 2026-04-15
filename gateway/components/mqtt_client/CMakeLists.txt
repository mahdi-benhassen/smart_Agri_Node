idf_component_register(SRCS "gw_mqtt_client.c" INCLUDE_DIRS "."
    REQUIRES esp_event mqtt agri_data_model)
