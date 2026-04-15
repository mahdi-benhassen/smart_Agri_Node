idf_component_register(
    SRCS "npk_rs485_driver.c"
    INCLUDE_DIRS "."
    REQUIRES driver crc_utils
)
