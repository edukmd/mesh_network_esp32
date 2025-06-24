idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES mqtt_mesh mqtt esp_wifi esp_driver_gpio nvs_flash json 
	)
