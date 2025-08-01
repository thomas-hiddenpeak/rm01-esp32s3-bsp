idf_component_register(SRCS "hardware_control.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver led_strip esp_timer
                       PRIV_REQUIRES freertos)
