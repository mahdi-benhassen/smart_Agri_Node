/*
 * rest_api.c — HTTP REST server: /api/v1/nodes, sensors, actuators, config, ota, health
 */
#include "rest_api.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "agri_data_model.h"
#include "zigbee_coordinator.h"
#include "system_monitor.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "REST_API";
static httpd_handle_t s_server = NULL;

#define REST_PORT  8080

/* ── GET /api/v1/health ────────────────────────────────────────────── */
static esp_err_t handler_health(httpd_req_t *req)
{
    char json[256];
    snprintf(json, sizeof(json),
             "{\"status\":\"ok\",\"heap\":%lu,\"min_heap\":%lu,\"uptime\":%lu}",
             (unsigned long)system_monitor_get_free_heap(),
             (unsigned long)system_monitor_get_min_heap(),
             (unsigned long)system_monitor_get_uptime_sec());

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, -1);
}

/* ── GET /api/v1/nodes ─────────────────────────────────────────────── */
static esp_err_t handler_nodes(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateArray();
    if (!root) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int count = zigbee_coordinator_get_node_count();
    for (int i = 0; i < count; i++) {
        agri_node_info_t info;
        if (zigbee_coordinator_get_node_info(i, &info) == ESP_OK) {
            cJSON *node = cJSON_CreateObject();
            cJSON_AddStringToObject(node, "device_id", info.device_id);
            cJSON_AddNumberToObject(node, "status", (int)info.status);
            cJSON_AddStringToObject(node, "fw_ver", info.fw_ver);
            cJSON_AddNumberToObject(node, "batt_mv", info.batt_mv);
            cJSON_AddNumberToObject(node, "rssi_dbm", info.rssi_dbm);
            cJSON_AddNumberToObject(node, "lqi", info.lqi);
            cJSON_AddNumberToObject(node, "zigbee_addr", info.zigbee_short_addr);
            cJSON_AddStringToObject(node, "last_seen", info.last_seen);
            cJSON_AddItemToArray(root, node);
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, -1);
        free(json);
    } else {
        httpd_resp_send_500(req);
    }

    return ESP_OK;
}

/* ── POST /api/v1/nodes/{id}/cmd ───────────────────────────────────── */
static esp_err_t handler_node_cmd(httpd_req_t *req)
{
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    agri_cmd_t cmd;
    if (agri_cmd_from_json(buf, &cmd) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    /* Extract node short address from URI (simplified) */
    uint16_t addr = 0;
    char *id_str = strstr(req->uri, "/nodes/");
    if (id_str) {
        id_str += 7;
        addr = (uint16_t)strtoul(id_str, NULL, 16);
    }

    esp_err_t err = zigbee_coordinator_send_cmd(addr, &cmd);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Send failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", -1);
    return ESP_OK;
}

/* ── GET /api/v1/config ────────────────────────────────────────────── */
static esp_err_t handler_config_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"farm_id\":\"farm01\",\"sleep_sec\":30}", -1);
    return ESP_OK;
}

/* ── Route Registration ───────────────────────────────────────────── */

esp_err_t rest_api_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = REST_PORT;
    config.max_uri_handlers = 12;
    config.stack_size = 6144;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register routes */
    httpd_uri_t health_uri   = { .uri = "/api/v1/health",   .method = HTTP_GET,  .handler = handler_health };
    httpd_uri_t nodes_uri    = { .uri = "/api/v1/nodes",    .method = HTTP_GET,  .handler = handler_nodes };
    httpd_uri_t cmd_uri      = { .uri = "/api/v1/nodes/*",  .method = HTTP_POST, .handler = handler_node_cmd };
    httpd_uri_t config_uri   = { .uri = "/api/v1/config",   .method = HTTP_GET,  .handler = handler_config_get };

    httpd_register_uri_handler(s_server, &health_uri);
    httpd_register_uri_handler(s_server, &nodes_uri);
    httpd_register_uri_handler(s_server, &cmd_uri);
    httpd_register_uri_handler(s_server, &config_uri);

    ESP_LOGI(TAG, "REST API started on port %d", REST_PORT);
    return ESP_OK;
}

void rest_api_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "REST API stopped");
    }
}
