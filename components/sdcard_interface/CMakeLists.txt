idf_component_register(
    SRCS "sdcard_interface.c"
    INCLUDE_DIRS "include"
    REQUIRES "esp_driver_sdmmc" "fatfs" "sdmmc" "vfs" "hardware_control" "main"
)
