idf_component_register(
    SRCS "nvs_config.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash agri_data_model
)
