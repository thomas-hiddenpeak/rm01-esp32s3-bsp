idf_component_register(SRCS "ethernet_interface.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_eth lwip console esp_driver_gpio esp_driver_spi driver
                       REQUIRES esp_timer freertos log esp_netif nvs_flash)
