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
#include "esp_mesh.h"

void mesh_connected_indicator(int layer);
void mesh_disconnected_indicator(void);

/**
 * @brief Initializes and starts the periodic task that sends node info (MAC, hops) to its parent.
 * 
 * Must be called after the mesh is initialized and a parent is available.
 */
void mqtt_mesh_start(void);

/**
 * @brief Callback function to be called when data is received in the mesh network.
 * 
 * @param from Pointer to the mesh address of the sender.
 * @param data Pointer to the data structure received.
 * 
 * Should be registered only at the root node.
 */
void mesh_received_cb(mesh_addr_t *from, mesh_data_t *data);

#endif // MQTT_MESH_H
