/**
 * @file mqtt_mesh.c
 * @brief Handles Wi-Fi mesh communication, including sending hop, MAC address, and parent info from each node up to the root node.
 */

#include "mqtt_mesh.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <string.h>
#include "driver/gpio.h"

#define TAG "MQTT_MESH"
#define MAX_CHILDREN 10


static esp_timer_handle_t periodic_timer;

/**
 * @brief Converts MAC address to human-readable string.
 */
static void mac_to_str(const uint8_t *mac, char *str)
{
    sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void periodic_send_callback(void *arg)
{
    char mac_str[18], parent_str[18], child_mac[18];
    uint8_t mac[6];
    mesh_addr_t parent;
    mesh_addr_t children[MAX_CHILDREN];
    int table_size = 0;

    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    mac_to_str(mac, mac_str);

    esp_mesh_get_parent_bssid(&parent);
    mac_to_str(parent.addr, parent_str);

    esp_mesh_get_routing_table((mesh_addr_t *)&children, sizeof(children), &table_size);
    int child_count = table_size / sizeof(mesh_addr_t);

    int layer = esp_mesh_get_layer();
    ESP_LOGI(TAG, "Layer atual: %d", layer);

    mesh_update_led_layer(layer);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "mac", mac_str);
    cJSON_AddStringToObject(json, "parent", esp_mesh_is_root() ? "null" : parent_str);
    cJSON_AddNumberToObject(json, "hops", layer);

    // Adiciona filhos
    cJSON *children_array = cJSON_CreateArray();
    for (int i = 0; i < child_count; i++) {
        mac_to_str(children[i].addr, child_mac);
        cJSON_AddItemToArray(children_array, cJSON_CreateString(child_mac));
    }
    cJSON_AddItemToObject(json, "children", children_array);

    const char *json_str = cJSON_PrintUnformatted(json);
    size_t len = strlen(json_str);
    uint8_t *buffer = calloc(1, len + 1);
    memcpy(buffer, json_str, len);

    ESP_LOGI(TAG, "JSON enviado: %s", json_str);

    if (esp_mesh_is_root()) {
        ESP_LOGI(TAG, "Root (self) info: %s", json_str);
        cJSON_free((void *)json_str);
        cJSON_Delete(json);
        free(buffer);
        return;
    }

    mesh_data_t data = {
        .data = buffer,
        .size = len + 1,
        .proto = MESH_PROTO_JSON,
        .tos = MESH_TOS_P2P
    };

    esp_err_t err = esp_mesh_send(NULL, &data, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send data to parent: %s", esp_err_to_name(err));
    }

    

    cJSON_free((void *)json_str);
    cJSON_Delete(json);
    free(buffer);
}


void mesh_connected_indicator(int layer) {
    ESP_LOGI("MESH", "CONNECTED to mesh at layer %d", layer);
}

void mesh_disconnected_indicator(void) {
    ESP_LOGI("MESH", "DISCONNECTED from mesh");
}


void mesh_update_led_layer(int layer) {
    switch (layer) {
        case 1:
            gpio_set_level(LED_GREEN, 0);
            gpio_set_level(LED_BLUE, 0);
            gpio_set_level(LED_RED, 0);
            break;
        case 2:
            gpio_set_level(LED_GREEN, 1);
            gpio_set_level(LED_BLUE, 0);
            gpio_set_level(LED_RED, 0);
            break;
        case 3:
            gpio_set_level(LED_GREEN, 0);
            gpio_set_level(LED_BLUE, 1);
            gpio_set_level(LED_RED, 0);
            break;
        case 4:
            gpio_set_level(LED_GREEN, 1);
            gpio_set_level(LED_BLUE, 1);
            gpio_set_level(LED_RED, 0);
            break;
        case 5:
            gpio_set_level(LED_GREEN, 0);
            gpio_set_level(LED_BLUE, 0);
            gpio_set_level(LED_RED, 1);
            break;
        case 6:
            gpio_set_level(LED_GREEN, 1);
            gpio_set_level(LED_BLUE, 0);
            gpio_set_level(LED_RED, 1);
            break;
        case 7:
            gpio_set_level(LED_GREEN, 0);
            gpio_set_level(LED_BLUE, 1);
            gpio_set_level(LED_RED, 1);
            break;
        case 8:
            gpio_set_level(LED_GREEN, 1);
            gpio_set_level(LED_BLUE, 1);
            gpio_set_level(LED_RED, 1);
            break;
    }
}


/**
 * @brief Initializes and starts the periodic reporting timer
 */
void mqtt_mesh_start()
{
    ESP_LOGI(TAG, "Starting MQTT Mesh logic");

    const esp_timer_create_args_t timer_args = {
        .callback = &periodic_send_callback,
        .name = "periodic_mesh_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 10 * 1000000)); // 10 seconds
}
