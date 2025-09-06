idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES device_interface console_interface ethernet_interface config_manager sdcard_interface web_server nvs_flash color_correction
                       INCLUDE_DIRS "")
