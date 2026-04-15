idf_component_register(SRCS "zigbee_coordinator.c" INCLUDE_DIRS "."
    REQUIRES esp_zigbee_lib agri_data_model agri_zigbee_clusters)
