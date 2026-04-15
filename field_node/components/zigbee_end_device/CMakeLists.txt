idf_component_register(
    SRCS "zigbee_end_device.c"
    INCLUDE_DIRS "."
    REQUIRES esp-zigbee-lib agri_data_model agri_zigbee_clusters
)
