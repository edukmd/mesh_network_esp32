/**
 * @file mqtt_mesh.h
 * @brief Header for MQTT Mesh communication logic using ESP-MESH.
 *
 * This module handles periodic reporting of MAC address and hop count
 * from each node to its parent in the mesh network. The root node prints
 * the information it receives.
 */

#ifndef MQTT_MESH_H
#define MQTT_MESH_H

#include "esp_err.h"

#define LED_RED    23
#define LED_BLUE    22
#define LED_GREEN    21

void mesh_connected_indicator(int layer);
void mesh_disconnected_indicator(void);

void mesh_update_led_layer(int layer);
void blink_all_leds(void);

#endif // MQTT_MESH_H
