/* Mesh Internal Communication Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <inttypes.h>
#include <string.h>

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "esp_wifi.h"
#include "hal/gpio_types.h"
#include "mqtt_client.h"
#include "mqtt_mesh.h"
#include "nvs_flash.h"

/*******************************************************
 *                Macros
 *******************************************************/

/*******************************************************
 *                Constants
 *******************************************************/
#define RX_SIZE (1500)
#define TX_SIZE (1460)

#define MESH_CONNECTION_PER_HOP 1
#define GPIO_OUTPUT_PIN_SEL ((1ULL << LED_RED) | (1ULL << LED_BLUE) | (1ULL << LED_GREEN))

#define MAX_ROUTING_TABLE_SIZE 20

static const char *TAG = "MAIN_CONFIG";
static int report_interval_ms = 10000;
static unsigned int blockTask = 0;
static int current_max_children = MESH_CONNECTION_PER_HOP;
static bool mesh_active = false;
volatile bool pending_mesh_restart = false;
static bool mesh_was_stopped = false;
static bool wifi_already_initialized = false;

// #define MQTT_IP "mqtt://192.168.10.127"
#define MQTT_IP "mqtt://192.168.50.208"

/*******************************************************
 *                Variable Definitions
 *******************************************************/
static const char *MESH_TAG = "mesh_main";
static const uint8_t MESH_ID[6] = {0x77, 0x77, 0x77, 0x77, 0x77, 0x77};
static uint8_t tx_buf[TX_SIZE] = {
    0,
};
static uint8_t rx_buf[RX_SIZE] = {
    0,
};
static bool is_running = true;
static bool is_mesh_connected = false;
static mesh_addr_t mesh_parent_addr;
static int mesh_layer = -1;
static esp_netif_t *netif_sta = NULL;
static esp_mqtt_client_handle_t mqtt_client = NULL;

// --- Funções utilitárias ---
static bool is_command_for_me(const char *target_mac);
static void forward_command_to_children(const char *data, size_t data_len);
static void get_mac_str(char *out, uint8_t mac[6]);
static const char *build_node_status_json(char *mac_str, char *parent_str, int hops, char children_output[][18], int child_count);

// --- Comandos P2P ---
static void handle_ping_response(void);
static void process_p2p_command(cJSON *cmd, const char *payload);
static void process_pong_response(cJSON *cmd, const char *payload);

// --- MQTT ---
static void mqtt_event_handler_cb(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void mqtt_app_start(void);

// --- Mesh ---
void esp_mesh_p2p_rx_main(void *arg);
esp_err_t esp_mesh_comm_p2p_start(void);
void mesh_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

// --- LED e GPIO ---
void led_gpio_init(void);
void mesh_connected_indicator(int layer);
void mesh_disconnected_indicator(void);
void mesh_update_led_layer(int layer);

// --- Tarefas ---
static void report_node_info_task(void *arg);
static void mesh_full_init_and_start(void);

// --- Main ---
void app_main(void);

static bool is_command_for_me(const char *target_mac) {
    char my_mac_str[18];
    uint8_t my_mac[6];
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    my_mac[5]++;
    sprintf(my_mac_str, "%02X:%02X:%02X:%02X:%02X:%02X",
            my_mac[0], my_mac[1], my_mac[2],
            my_mac[3], my_mac[4], my_mac[5]);

    return strcmp(my_mac_str, target_mac) == 0;
}

static void forward_command_to_children(const char *data, size_t data_len) {
    mesh_addr_t children[MAX_ROUTING_TABLE_SIZE];
    int table_size = 0;

    if (esp_mesh_get_routing_table(children, MAX_ROUTING_TABLE_SIZE * 6, &table_size) != ESP_OK) {
        ESP_LOGW("MQTT CMD", "⚠️ Falha ao obter tabela de roteamento");
        return;
    }

    mesh_data_t fwd_data = {
        .proto = MESH_PROTO_BIN,
        .tos = MESH_TOS_P2P,
        .data = (uint8_t *)data,
        .size = data_len};

    for (int i = 0; i < table_size; ++i) {
        if (i == 0) continue;
        esp_err_t err = esp_mesh_send(&children[i], &fwd_data, MESH_DATA_P2P, NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGW("MQTT CMD", "❌ Falha ao enviar para filho %d", i);
        } else {
            ESP_LOGI("MQTT CMD", "📤 Enviado para filho %d", i);
        }
    }
}

/**
 * @brief Responde ao comando "ping" com uma mensagem "pong" que será encaminhada até o nó raiz.
 */
static void handle_ping_response(void) {
    ESP_LOGI("PING_HANDLER", "🏓 Comando ping recebido. Preparando resposta...");

    char my_mac_str[18];
    uint8_t my_mac[6];
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    my_mac[5]++;
    sprintf(my_mac_str, "%02X:%02X:%02X:%02X:%02X:%02X",
            my_mac[0], my_mac[1], my_mac[2],
            my_mac[3], my_mac[4], my_mac[5]);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "pong");
    cJSON_AddStringToObject(resp, "mac", my_mac_str);

    const char *json_str = cJSON_PrintUnformatted(resp);

    if (esp_mesh_is_root() && mqtt_client) {
        ESP_LOGI("PING_HANDLER", "📤 Enviando resposta PONG via MQTT (sou root)");
        esp_mqtt_client_publish(mqtt_client, "mesh/network/info", json_str, 0, 1, 0);
    } else {
        mesh_data_t response = {
            .proto = MESH_PROTO_BIN,
            .tos = MESH_TOS_P2P,
            .data = (uint8_t *)json_str,
            .size = strlen(json_str) + 1};

        esp_err_t err = esp_mesh_send(NULL, &response, 0, NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGW("PING_HANDLER", "❌ Falha ao enviar resposta PONG: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI("PING_HANDLER", "📤 Resposta PONG enviada para o pai");
        }
    }

    cJSON_free((void *)json_str);
    cJSON_Delete(resp);
}

static void process_p2p_command(cJSON *cmd, const char *payload) {
    cJSON *target = cJSON_GetObjectItem(cmd, "target");
    cJSON *action = cJSON_GetObjectItem(cmd, "action");

    if (!target || !action || !cJSON_IsString(target) || !cJSON_IsString(action)) return;

    if (is_command_for_me(target->valuestring)) {
        if (strcmp(action->valuestring, "blink") == 0) {
            ESP_LOGI("P2P_CMD", "✨ Comando blink recebido");
            blink_all_leds();
        } else if (strcmp(action->valuestring, "ping") == 0) {
            ESP_LOGI("P2P_CMD", "🏓 Comando ping recebido");
            handle_ping_response();
        }
    } else {
        ESP_LOGI("P2P_CMD", "🔁 Comando não é para mim, repassando...");
        // forward_command_to_children(payload, strlen(payload) + 1);
    }
}

static void process_pong_response(cJSON *cmd, const char *payload) {
    if (esp_mesh_is_root() && mqtt_client) {
        ESP_LOGI("MESH", "📨 Resposta PONG recebida: %s", payload);
        esp_mqtt_client_publish(mqtt_client, "mesh/network/info", payload, 0, 1, 0);
    } else {
        ESP_LOGI("MESH", "🔁 Encaminhando resposta PONG para o pai");
        mesh_data_t forward = {
            .proto = MESH_PROTO_BIN,
            .tos = MESH_TOS_P2P,
            .data = (uint8_t *)payload,
            .size = strlen(payload) + 1};
        esp_mesh_send(NULL, &forward, 0, NULL, 0);
    }
}

static const char *build_node_status_json(char *mac_str, char *parent_str, int hops, char children_output[][18], int child_count) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "mac", mac_str);
    cJSON_AddStringToObject(json, "parent", esp_mesh_is_root() ? "null" : parent_str);
    cJSON_AddNumberToObject(json, "hops", hops);

    cJSON *children_array = cJSON_CreateArray();
    for (int i = 0; i < child_count; ++i) {
        cJSON_AddItemToArray(children_array, cJSON_CreateString(children_output[i]));
    }
    cJSON_AddItemToObject(json, "children", children_array);

    const char *json_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    return json_str;
}

/**
 * @brief Callback de eventos MQTT
 */
static void mqtt_event_handler_cb(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI("MQTT HANDLER", "MQTT connected");
            esp_mqtt_client_subscribe(client, "mesh/cmd", 0);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW("MQTT HANDLER", "MQTT disconnected");
            break;

        case MQTT_EVENT_DATA: {
            ESP_LOGI("MQTT HANDLER", "MQTT data received: topic=%.*s, data=%.*s",
                     event->topic_len, event->topic,
                     event->data_len, event->data);

            cJSON *cmd = cJSON_ParseWithLength(event->data, event->data_len);
            if (!cmd) {
                ESP_LOGW("MQTT CMD", "⚠️ Comando inválido recebido");
                break;
            }

            cJSON *interval = cJSON_GetObjectItem(cmd, "interval");
            if (interval && cJSON_IsNumber(interval)) {
                cJSON *max_children = cJSON_GetObjectItem(cmd, "max_children");

                // Atualiza intervalo
                if (interval->valueint != 0) {
                    blockTask = 0;
                    report_interval_ms = interval->valueint;
                    ESP_LOGW("MQTT CMD", "🕒 Novo intervalo de envio: %d ms", report_interval_ms);
                } else {
                    blockTask = 1;
                    ESP_LOGW("MQTT CMD", "🛑 Task de envio bloqueada por intervalo 0");
                }

                forward_command_to_children(event->data, event->data_len);

                // Atualiza max_children e reconfigura mesh
                if (max_children && cJSON_IsNumber(max_children)) {
                    int new_max = max_children->valueint;

                    if (new_max <= 0 || new_max > MAX_ROUTING_TABLE_SIZE) {
                        ESP_LOGW("MQTT CMD", "⚠️ Valor inválido para max_children: %d", new_max);
                    } else if (new_max != current_max_children) {
                        current_max_children = new_max;
                        ESP_LOGW("MQTT CMD", "🆕 max_children alterado para %d, reconfiguração agendada...", current_max_children);
                        pending_mesh_restart = true;
                        mesh_was_stopped = false;
                    } else {
                        ESP_LOGI("MQTT CMD", "ℹ️ max_children já está em %d, sem necessidade de reconfigurar", current_max_children);
                    }
                }
            } else {
                cJSON *target = cJSON_GetObjectItem(cmd, "target");
                cJSON *action = cJSON_GetObjectItem(cmd, "action");

                if (target && action && cJSON_IsString(target) && cJSON_IsString(action)) {
                    if (is_command_for_me(target->valuestring)) {
                        if (strcmp(action->valuestring, "blink") == 0) {
                            ESP_LOGI("MQTT CMD", "✨ Blink recebido para mim");
                            blink_all_leds();
                        } else if (strcmp(action->valuestring, "ping") == 0) {
                            ESP_LOGI("MQTT CMD", "🏓 Comando ping é para mim, respondendo diretamente");
                            handle_ping_response();  // responderá via MQTT no nó raiz
                        }
                    } else {
                        ESP_LOGI("MQTT CMD", "🔁 Repassando comando para os filhos do nó raiz...");
                        forward_command_to_children(event->data, event->data_len);
                    }
                }
            }

            cJSON_Delete(cmd);
            break;
        }

        default:
            ESP_LOGD("MQTT HANDLER", "Other event id:%d", event->event_id);
            break;
    }
}

static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_IP,  // Ex: CONFIG_BROKER_URL
        .network.reconnect_timeout_ms = 5000};
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler_cb, NULL);
    esp_mqtt_client_start(mqtt_client);
}

static void get_mac_str(char *out, uint8_t mac[6]) {
    sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void check_and_reconfigure_mesh(void) {
    if (pending_mesh_restart) {
        if (mesh_active && !mesh_was_stopped) {
            ESP_LOGI("MESH_RECONFIG", "🛑 Parando mesh para reconfiguração...");
            mesh_update_led_layer(8);
            ESP_ERROR_CHECK(esp_mesh_stop());
            mesh_was_stopped = true;

        } else if (!mesh_active && mesh_was_stopped) {
            ESP_LOGI("MESH_RECONFIG", "🚀 Reiniciando mesh com nova configuração...");
            ESP_ERROR_CHECK(esp_mesh_deinit());
            mesh_full_init_and_start();
            mesh_was_stopped = false;
            pending_mesh_restart = false;
        }
    }
}

void mesh_reconfig_task(void *arg) {
    while (true) {
        check_and_reconfigure_mesh();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void report_node_info_task(void *arg) {
    mesh_data_t data;
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;
    data.data = tx_buf;

    mesh_addr_t parent;
    mesh_addr_t children[MAX_ROUTING_TABLE_SIZE];
    char mac_str[18], parent_str[18], children_mac[MAX_ROUTING_TABLE_SIZE][18];

    while (true) {
        vTaskDelay(report_interval_ms / portTICK_PERIOD_MS);

        if (blockTask || !mesh_active) {
            continue;
        }

        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        mac[5]++;
        get_mac_str(mac_str, mac);

        esp_mesh_get_parent_bssid(&parent);
        get_mac_str(parent_str, parent.addr);

        int layer = esp_mesh_get_layer();
        mesh_update_led_layer(layer);

        int table_size = 0;
        esp_mesh_get_routing_table(children, MAX_ROUTING_TABLE_SIZE * 6, &table_size);

        int child_count = 0;
        for (int i = 0; i < table_size; i++) {
            if (i != 0) {
                children[i].addr[5]++;
                get_mac_str(children_mac[child_count++], children[i].addr);
            }
        }

        const char *json_str = build_node_status_json(mac_str, parent_str, layer, children_mac, child_count);
        size_t len = strlen(json_str);
        if (len >= TX_SIZE) len = TX_SIZE - 1;

        memcpy(tx_buf, json_str, len);
        tx_buf[len] = '\0';
        data.size = len + 1;

        if (esp_mesh_is_root()) {
            esp_mqtt_client_publish(mqtt_client, "mesh/network/info", (const char *)tx_buf, data.size - 1, 1, 0);
        } else {
            esp_mesh_send(NULL, &data, 0, NULL, 0);
        }

        cJSON_free((void *)json_str);
    }
}

/*******************************************************
 *                Function Declarations
 *******************************************************/

/*******************************************************
 *                Function Definitions
 *******************************************************/

void esp_mesh_p2p_rx_main(void *arg) {
    mesh_data_t data;
    mesh_addr_t from;
    int flag;
    data.data = rx_buf;
    data.size = RX_SIZE;

    while (true) {
        data.size = RX_SIZE;
        if (esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0) == ESP_OK && data.size > 0) {
            char *payload = (char *)data.data;

            cJSON *cmd = cJSON_Parse(payload);
            if (!cmd) {
                ESP_LOGW("MESH_RX", "⚠️ JSON inválido recebido: %s", payload);
                continue;
            }

            cJSON *type = cJSON_GetObjectItem(cmd, "type");
            if (type && cJSON_IsString(type) && strcmp(type->valuestring, "pong") == 0) {
                process_pong_response(cmd, payload);
                cJSON_Delete(cmd);
                continue;
            }

            cJSON *interval = cJSON_GetObjectItem(cmd, "interval");
            if (interval && cJSON_IsNumber(interval)) {
                cJSON *max_children = cJSON_GetObjectItem(cmd, "max_children");

                // Atualiza intervalo
                if (interval->valueint != 0) {
                    blockTask = 0;
                    report_interval_ms = interval->valueint;
                    ESP_LOGW("MQTT CMD", "🕒 Novo intervalo de envio: %d ms", report_interval_ms);
                } else {
                    blockTask = 1;
                    ESP_LOGW("MQTT CMD", "🛑 Task de envio bloqueada por intervalo 0");
                }

                if (max_children && cJSON_IsNumber(max_children)) {
                    int new_max = max_children->valueint;

                    if (new_max <= 0 || new_max > MAX_ROUTING_TABLE_SIZE) {
                        ESP_LOGW("MQTT CMD", "⚠️ Valor inválido para max_children: %d", new_max);
                    } else if (new_max != current_max_children) {
                        current_max_children = new_max;
                        ESP_LOGW("MQTT CMD", "🆕 max_children alterado para %d, reconfiguração agendada...", current_max_children);
                        pending_mesh_restart = true;
                        mesh_was_stopped = false;
                    } else {
                        ESP_LOGI("MQTT CMD", "ℹ️ max_children já está em %d, sem necessidade de reconfigurar", current_max_children);
                    }
                }

                continue;
            }

            if (esp_mesh_is_root() && mqtt_client) {
                if (!cJSON_GetObjectItem(cmd, "target")) {
                    esp_mqtt_client_publish(mqtt_client, "mesh/network/info", payload, 0, 1, 0);
                    cJSON_Delete(cmd);
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                    continue;
                }
                ESP_LOGI("NODE_JSON", "🎯 Comando com 'target' recebido, não publicado no MQTT.");
            }

            process_p2p_command(cmd, payload);
            cJSON_Delete(cmd);
        }
    }
}

esp_err_t esp_mesh_comm_p2p_start(void) {
    static bool started = false;
    if (!started) {
        started = true;
        xTaskCreate(report_node_info_task, "report_info", 4096, NULL, 5, NULL);
        xTaskCreate(esp_mesh_p2p_rx_main, "rx_task", 4096, NULL, 5, NULL);
        xTaskCreate(mesh_reconfig_task, "mesh_reconfig", 4096, NULL, 7, NULL);
    }
    return ESP_OK;
}

void mesh_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data) {
    mesh_addr_t id = {
        0,
    };
    static uint16_t last_layer = 0;

    switch (event_id) {
        case MESH_EVENT_STARTED: {
            mesh_active = true;
            esp_mesh_get_id(&id);
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_MESH_STARTED>ID:" MACSTR "", MAC2STR(id.addr));
            is_mesh_connected = false;
            mesh_layer = esp_mesh_get_layer();
        } break;
        case MESH_EVENT_STOPPED: {
            mesh_active = false;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOPPED>");
            is_mesh_connected = false;
            mesh_layer = esp_mesh_get_layer();

        } break;
        case MESH_EVENT_CHILD_CONNECTED: {
            mesh_event_child_connected_t *child_connected = (mesh_event_child_connected_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_CONNECTED>aid:%d, " MACSTR "",
                     child_connected->aid,
                     MAC2STR(child_connected->mac));
        } break;
        case MESH_EVENT_CHILD_DISCONNECTED: {
            mesh_event_child_disconnected_t *child_disconnected = (mesh_event_child_disconnected_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_DISCONNECTED>aid:%d, " MACSTR "",
                     child_disconnected->aid,
                     MAC2STR(child_disconnected->mac));
        } break;
        case MESH_EVENT_ROUTING_TABLE_ADD: {
            mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
            ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_ADD>add %d, new:%d, layer:%d",
                     routing_table->rt_size_change,
                     routing_table->rt_size_new, mesh_layer);
        } break;
        case MESH_EVENT_ROUTING_TABLE_REMOVE: {
            mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
            ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_REMOVE>remove %d, new:%d, layer:%d",
                     routing_table->rt_size_change,
                     routing_table->rt_size_new, mesh_layer);
        } break;
        case MESH_EVENT_NO_PARENT_FOUND: {
            mesh_event_no_parent_found_t *no_parent = (mesh_event_no_parent_found_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_NO_PARENT_FOUND>scan times:%d",
                     no_parent->scan_times);
        }
        /* TODO handler for the failure */
        break;
        case MESH_EVENT_PARENT_CONNECTED: {
            mesh_event_connected_t *connected = (mesh_event_connected_t *)event_data;
            esp_mesh_get_id(&id);
            mesh_layer = connected->self_layer;
            mesh_update_led_layer(mesh_layer);
            memcpy(&mesh_parent_addr.addr, connected->connected.bssid, 6);
            ESP_LOGI(MESH_TAG,
                     "<MESH_EVENT_PARENT_CONNECTED>layer:%d-->%d, parent:" MACSTR "%s, ID:" MACSTR ", duty:%d",
                     last_layer, mesh_layer, MAC2STR(mesh_parent_addr.addr),
                     esp_mesh_is_root() ? "<ROOT>" : (mesh_layer == 2) ? "<layer2>"
                                                                       : "",
                     MAC2STR(id.addr), connected->duty);
            last_layer = mesh_layer;
            mesh_connected_indicator(mesh_layer);
            is_mesh_connected = true;
            if (esp_mesh_is_root()) {
                esp_netif_dhcpc_stop(netif_sta);
                esp_netif_dhcpc_start(netif_sta);

                if (mqtt_client == NULL) {
                    mqtt_app_start();  // Se nunca inicializou
                } else {
                    esp_mqtt_client_stop(mqtt_client);
                    esp_mqtt_client_start(mqtt_client);  // Reinicia o cliente MQTT se já existia
                    ESP_LOGI(MESH_TAG, "🔁 Reiniciando conexão MQTT após reconexão como root.");
                }
            }
            esp_mesh_comm_p2p_start();
        } break;
        case MESH_EVENT_PARENT_DISCONNECTED: {
            mesh_event_disconnected_t *disconnected = (mesh_event_disconnected_t *)event_data;
            ESP_LOGI(MESH_TAG,
                     "<MESH_EVENT_PARENT_DISCONNECTED>reason:%d",
                     disconnected->reason);
            is_mesh_connected = false;
            mesh_disconnected_indicator();
            mesh_layer = esp_mesh_get_layer();
        } break;
        case MESH_EVENT_LAYER_CHANGE: {
            mesh_event_layer_change_t *layer_change = (mesh_event_layer_change_t *)event_data;
            mesh_layer = layer_change->new_layer;
            mesh_update_led_layer(mesh_layer);
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_LAYER_CHANGE>layer:%d-->%d%s",
                     last_layer, mesh_layer,
                     esp_mesh_is_root() ? "<ROOT>" : (mesh_layer == 2) ? "<layer2>"
                                                                       : "");
            last_layer = mesh_layer;
            mesh_connected_indicator(mesh_layer);
        } break;
        case MESH_EVENT_ROOT_ADDRESS: {
            mesh_event_root_address_t *root_addr = (mesh_event_root_address_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_ADDRESS>root address:" MACSTR "",
                     MAC2STR(root_addr->addr));
        } break;
        case MESH_EVENT_VOTE_STARTED: {
            mesh_event_vote_started_t *vote_started = (mesh_event_vote_started_t *)event_data;
            ESP_LOGI(MESH_TAG,
                     "<MESH_EVENT_VOTE_STARTED>attempts:%d, reason:%d, rc_addr:" MACSTR "",
                     vote_started->attempts,
                     vote_started->reason,
                     MAC2STR(vote_started->rc_addr.addr));
        } break;
        case MESH_EVENT_VOTE_STOPPED: {
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_VOTE_STOPPED>");
            break;
        }
        case MESH_EVENT_ROOT_SWITCH_REQ: {
            mesh_event_root_switch_req_t *switch_req = (mesh_event_root_switch_req_t *)event_data;
            ESP_LOGI(MESH_TAG,
                     "<MESH_EVENT_ROOT_SWITCH_REQ>reason:%d, rc_addr:" MACSTR "",
                     switch_req->reason,
                     MAC2STR(switch_req->rc_addr.addr));
        } break;
        case MESH_EVENT_ROOT_SWITCH_ACK: {
            /* new root */
            mesh_layer = esp_mesh_get_layer();
            esp_mesh_get_parent_bssid(&mesh_parent_addr);
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_SWITCH_ACK>layer:%d, parent:" MACSTR "", mesh_layer, MAC2STR(mesh_parent_addr.addr));
        } break;
        case MESH_EVENT_TODS_STATE: {
            mesh_event_toDS_state_t *toDs_state = (mesh_event_toDS_state_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_TODS_REACHABLE>state:%d", *toDs_state);
        } break;
        case MESH_EVENT_ROOT_FIXED: {
            mesh_event_root_fixed_t *root_fixed = (mesh_event_root_fixed_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_FIXED>%s",
                     root_fixed->is_fixed ? "fixed" : "not fixed");
        } break;
        case MESH_EVENT_ROOT_ASKED_YIELD: {
            mesh_event_root_conflict_t *root_conflict = (mesh_event_root_conflict_t *)event_data;
            ESP_LOGI(MESH_TAG,
                     "<MESH_EVENT_ROOT_ASKED_YIELD>" MACSTR ", rssi:%d, capacity:%d",
                     MAC2STR(root_conflict->addr),
                     root_conflict->rssi,
                     root_conflict->capacity);
        } break;
        case MESH_EVENT_CHANNEL_SWITCH: {
            mesh_event_channel_switch_t *channel_switch = (mesh_event_channel_switch_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHANNEL_SWITCH>new channel:%d", channel_switch->channel);
        } break;
        case MESH_EVENT_SCAN_DONE: {
            mesh_event_scan_done_t *scan_done = (mesh_event_scan_done_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_SCAN_DONE>number:%d",
                     scan_done->number);
        } break;
        case MESH_EVENT_NETWORK_STATE: {
            mesh_event_network_state_t *network_state = (mesh_event_network_state_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_NETWORK_STATE>is_rootless:%d",
                     network_state->is_rootless);
        } break;
        case MESH_EVENT_STOP_RECONNECTION: {
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOP_RECONNECTION>");
        } break;
        case MESH_EVENT_FIND_NETWORK: {
            mesh_event_find_network_t *find_network = (mesh_event_find_network_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_FIND_NETWORK>new channel:%d, router BSSID:" MACSTR "",
                     find_network->channel, MAC2STR(find_network->router_bssid));
        } break;
        case MESH_EVENT_ROUTER_SWITCH: {
            mesh_event_router_switch_t *router_switch = (mesh_event_router_switch_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROUTER_SWITCH>new router:%s, channel:%d, " MACSTR "",
                     router_switch->ssid, router_switch->channel, MAC2STR(router_switch->bssid));
        } break;
        case MESH_EVENT_PS_PARENT_DUTY: {
            mesh_event_ps_duty_t *ps_duty = (mesh_event_ps_duty_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_PS_PARENT_DUTY>duty:%d", ps_duty->duty);
        } break;
        case MESH_EVENT_PS_CHILD_DUTY: {
            mesh_event_ps_duty_t *ps_duty = (mesh_event_ps_duty_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_PS_CHILD_DUTY>cidx:%d, " MACSTR ", duty:%d", ps_duty->child_connected.aid - 1,
                     MAC2STR(ps_duty->child_connected.mac), ps_duty->duty);
        } break;
        default:
            ESP_LOGI(MESH_TAG, "unknown id:%" PRId32 "", event_id);
            break;
    }
}

void ip_event_handler(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(MESH_TAG, "<IP_EVENT_STA_GOT_IP>IP:" IPSTR, IP2STR(&event->ip_info.ip));

    if (esp_mesh_is_root()) {
        ESP_LOGI(MESH_TAG, "entrou aqui no ROOT");
        mqtt_app_start();  // <-- Alternativa segura
    }
}

void led_gpio_init(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LED_RED) | (1ULL << LED_GREEN) | (1ULL << LED_BLUE),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    gpio_set_level(LED_RED, 1);
    gpio_set_level(LED_GREEN, 1);
    gpio_set_level(LED_BLUE, 1);
}

static void mesh_full_init_and_start(void) {
    ESP_LOGI("MESH_RECONFIG", "🔁 Inicializando Mesh (reconfiguração)...");

    if (!wifi_already_initialized) {
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&config));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
        ESP_ERROR_CHECK(esp_wifi_start());
        wifi_already_initialized = true;
    }

    ESP_ERROR_CHECK(esp_mesh_init());
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mesh_set_topology(CONFIG_MESH_TOPOLOGY));
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(CONFIG_MESH_MAX_LAYER));
    ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(1));
    ESP_ERROR_CHECK(esp_mesh_set_xon_qsize(128));
    ESP_ERROR_CHECK(esp_mesh_disable_ps());
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(20));

    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    memcpy((uint8_t *)&cfg.mesh_id, MESH_ID, 6);
    cfg.channel = CONFIG_MESH_CHANNEL;
    cfg.router.ssid_len = strlen(CONFIG_MESH_ROUTER_SSID);
    memcpy((uint8_t *)&cfg.router.ssid, CONFIG_MESH_ROUTER_SSID, cfg.router.ssid_len);
    memcpy((uint8_t *)&cfg.router.password, CONFIG_MESH_ROUTER_PASSWD, strlen(CONFIG_MESH_ROUTER_PASSWD));

    cfg.mesh_ap.max_connection = current_max_children;
    cfg.mesh_ap.nonmesh_max_connection = CONFIG_MESH_NON_MESH_AP_CONNECTIONS;
    memcpy((uint8_t *)&cfg.mesh_ap.password, CONFIG_MESH_AP_PASSWD, strlen(CONFIG_MESH_AP_PASSWD));

    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(CONFIG_MESH_AP_AUTHMODE));
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));

    vTaskDelay((esp_random() % 5000) / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(esp_mesh_start());

    ESP_LOGI("MESH_RECONFIG", "✅ Mesh reconfigurada com sucesso");
}

void app_main(void) {
    ESP_LOGI(TAG, "Inicializando Mesh com configurações otimizadas...");
    led_gpio_init();

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&netif_sta, NULL));

    mesh_full_init_and_start();  // Chamada centralizada

    // Inicializa as tasks Mesh P2P e monitoramento
    ESP_ERROR_CHECK(esp_mesh_comm_p2p_start());

    ESP_LOGI(MESH_TAG, "Mesh inicializada, heap:%" PRId32 ", topo:%s, ps:%d",
             esp_get_minimum_free_heap_size(),
             esp_mesh_get_topology() ? "chain" : "tree",
             esp_mesh_is_ps_enabled());
}
