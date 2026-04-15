idf_component_register(
    SRCS "ota_node.c"
    INCLUDE_DIRS "."
    REQUIRES esp_ota_ops esp_partition app_update
)
