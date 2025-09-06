idf_component_register(SRCS "hardware_control.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver led_strip esp_timer json esp_http_client ethernet_interface color_correction esp_adc
                       PRIV_REQUIRES freertos)
