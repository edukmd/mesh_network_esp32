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

#define TAG "MQTT_MESH"

static esp_timer_handle_t periodic_timer;

/**
 * @brief Converts MAC address to human-readable string.
 */
static void mac_to_str(const uint8_t *mac, char *str)
{
    sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * @brief Timer callback to send node info to parent
 */
static void periodic_send_callback(void *arg)
{
    char mac_str[18], parent_str[18];
    uint8_t mac[6];
    mesh_addr_t parent;

    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    mac_to_str(mac, mac_str);

    int layer = esp_mesh_get_layer();
    esp_mesh_get_parent_bssid(&parent);
    mac_to_str(parent.addr, parent_str);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "mac", mac_str);
    cJSON_AddStringToObject(json, "parent", esp_mesh_is_root() ? "null" : parent_str);
    cJSON_AddNumberToObject(json, "hops", layer);

    if (esp_mesh_is_root()) {
        ESP_LOGI(TAG, "Root (self) info: %s", cJSON_PrintUnformatted(json));
        cJSON_Delete(json);
        return;
    }

    const char *json_str = cJSON_PrintUnformatted(json);
    size_t len = strlen(json_str);
    uint8_t *buffer = calloc(1, len + 1);
    memcpy(buffer, json_str, len);

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

    free(buffer);
    cJSON_Delete(json);
}

void mesh_connected_indicator(int layer) {
    ESP_LOGI("MESH", "CONNECTED to mesh at layer %d", layer);
}

void mesh_disconnected_indicator(void) {
    ESP_LOGI("MESH", "DISCONNECTED from mesh");
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
