/**
 * @file mqtt_mesh.c
 * @brief Handles Wi-Fi mesh communication, including sending hop, MAC address, and parent info from each node up to the root node.
 */

#include "mqtt_mesh.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "driver/gpio.h"

#define TAG "MQTT_MESH"

static int layerLastState = 0;

/**
 * @brief Converts MAC address to human-readable string.
 */
static void mac_to_str(const uint8_t *mac, char *str)
{
    sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void mesh_connected_indicator(int layer)
{
    ESP_LOGI("MESH", "CONNECTED to mesh at layer %d", layer);
}

void mesh_disconnected_indicator(void)
{
    ESP_LOGI("MESH", "DISCONNECTED from mesh");
}

void mesh_update_led_layer(int layer)
{

    layerLastState = layer;

    switch (layer)
    {
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
 * @brief Pisca os LEDs RGB conectados nas GPIOs definidas como LED_RED, LED_GREEN e LED_BLUE.
 *
 * Essa função é usada para indicar visualmente ações nos nós da rede mesh.
 * Cada LED pisca duas vezes com um intervalo de 200 ms.
 */
void blink_all_leds(void)
{
    const int delay_ms = 200;

    for (int i = 0; i < 2; i++)
    {
        gpio_set_level(LED_RED, 0);
        gpio_set_level(LED_GREEN, 0);
        gpio_set_level(LED_BLUE, 0);
        vTaskDelay(delay_ms / portTICK_PERIOD_MS);
        gpio_set_level(LED_RED, 1);
        gpio_set_level(LED_GREEN, 1);
        gpio_set_level(LED_BLUE, 1);
        vTaskDelay(delay_ms / portTICK_PERIOD_MS);
    }

    gpio_set_level(LED_RED, 0);
    gpio_set_level(LED_GREEN, 0);
    gpio_set_level(LED_BLUE, 0);
    vTaskDelay(500 / portTICK_PERIOD_MS);

    mesh_update_led_layer(layerLastState);
}
