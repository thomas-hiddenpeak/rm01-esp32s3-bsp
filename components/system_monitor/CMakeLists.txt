idf_component_register(SRCS "system_monitor.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_timer spi_flash
                       PRIV_REQUIRES freertos)
