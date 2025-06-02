/**
 * @file mqtt_mesh.c
 * @brief Handles Wi-Fi mesh communication, including sending hop and MAC address information from each node to its parent up to the root node.
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

static mesh_addr_t parent_addr;
static mesh_addr_t self_addr;
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
    char mac_str[18];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    mac_to_str(mac, mac_str);

    int layer = esp_mesh_get_layer();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "mac", mac_str);
    cJSON_AddNumberToObject(root, "hops", layer);

    if (esp_mesh_is_root()) {
        // Root node stores received JSONs, skipped here
        cJSON_Delete(root);
        return;
    }

    mesh_data_t data = {
        .data = (uint8_t *)cJSON_PrintUnformatted(root),
        .size = strlen((char *)data.data) + 1,
        .proto = MESH_PROTO_JSON,
        .tos = MESH_TOS_P2P
    };

    esp_mesh_get_parent_bssid(&parent_addr);
    esp_err_t err = esp_mesh_send(&parent_addr, &data, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send data to parent: %s", esp_err_to_name(err));
    }

    cJSON_free(data.data);
    cJSON_Delete(root);
}

/**
 * @brief Handles received messages on the mesh network
 */
void mesh_received_cb(mesh_addr_t *from, mesh_data_t *data)
{
    if (!esp_mesh_is_root()) return;

    // Root receives and prints data
    ESP_LOGI(TAG, "Root received: %.*s", data->size, data->data);
}

/**
 * @brief Initializes and starts the periodic reporting timer
 */
void mqtt_mesh_start()
{
    ESP_LOGI(TAG, "Starting MQTT Mesh logic");

    esp_mesh_get_parent_bssid(&parent_addr);
    esp_mesh_get_id(&self_addr);

    const esp_timer_create_args_t timer_args = {
        .callback = &periodic_send_callback,
        .name = "periodic_mesh_timer"
    };
    esp_timer_create(&timer_args, &periodic_timer);
    esp_timer_start_periodic(periodic_timer, 10 * 1000000); // 10 seconds
}
