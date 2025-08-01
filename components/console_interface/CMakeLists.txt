# Console Interface Component CMakeLists.txt

set(component_sources
    "console_interface.c"
)

set(component_headers
    "include/console_interface.h"
)

idf_component_register(
    SRCS ${component_sources}
    INCLUDE_DIRS "include"
    REQUIRES 
        freertos
        esp_common
        esp_timer
        console
        device_interface
        hardware_control 
        system_monitor
    PRIV_REQUIRES
        driver
)
