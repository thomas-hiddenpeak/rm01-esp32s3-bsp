idf_component_register(SRCS "device_interface.c"
                       INCLUDE_DIRS "include"
                       REQUIRES hardware_control system_monitor nvs_flash
                       PRIV_REQUIRES freertos)
