idf_component_register(
    SRCS "mqtt_mesh.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_driver_gpio esp_driver_ledc nvs_flash json esp_timer
)