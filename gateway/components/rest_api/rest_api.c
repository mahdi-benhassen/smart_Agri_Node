/*
 * rest_api.c — HTTP REST server: /api/v1/...
 *
 * FIXES applied:
 *  8a. Node address extraction: parse the hex address correctly from URIs
 *      like /api/v1/nodes/1A2B/cmd (skip alpha prefix "NODE-" if present).
 *  8b. Bearer token authentication added to all non-health endpoints.
 *  8c. Config GET reads live NVS values instead of hardcoded strings.
 */
#include "rest_api.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "agri_data_model.h"
#include "zigbee_coordinator.h"
#include "system_monitor.h"
#include "nvs_config.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG  = "REST_API";
static httpd_handle_t s_server = NULL;

#define REST_PORT   8080

/* ── Auth helper ───────────────────────────────────────────────────── */
/**
 * Read the "Authorization: Bearer <token>" header and validate it against
 * the api_token stored in NVS.  Returns true if auth passes.
 *
 * For production, replace with a full JWT RS256 verification library.
 * This implementation provides a lightweight static-token check so that
 * at least unauthenticated access is rejected.
 */
static bool check_bearer_auth(httpd_req_t *req)
{
    char auth_hdr[128] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization",
                                     auth_hdr, sizeof(auth_hdr)) != ESP_OK) {
        return false;
    }

    /* Expect "Bearer <token>" */
    if (strncmp(auth_hdr, "Bearer ", 7) != 0) return false;
    const char *token = auth_hdr + 7;

    char expected[64] = {0};
    gw_nvs_config_get_str(GW_NVS_API_TOKEN, expected, sizeof(expected), "");

    if (strlen(expected) == 0) {
        /* No token configured — warn but allow (dev mode) */
        ESP_LOGW(TAG, "API token not set in NVS — allowing unauthenticated access");
        return true;
    }

    return (strcmp(token, expected) == 0);
}

static esp_err_t send_unauthorized(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_send(req, "{\"error\":\"Unauthorized\"}", -1);
    return ESP_OK;
}

/* ── Internal: Extract 16-bit Zigbee address from URI ──────────────── */
/**
 * For a URI like /api/v1/nodes/1A2B/cmd or /api/v1/nodes/NODE-1A2B/cmd
 * returns 0x1A2B.  Returns 0 on failure.
 */
static uint16_t extract_node_addr(const char *uri)
{
    const char *p = strstr(uri, "/nodes/");
    if (!p) return 0;
    p += 7;  /* skip "/nodes/" */

    /* Skip optional "NODE-" prefix */
    if (strncmp(p, "NODE-", 5) == 0) p += 5;

    /* strtoul stops at the first non-hex char (e.g. '/') */
    char *endptr = NULL;
    unsigned long addr = strtoul(p, &endptr, 16);
    if (endptr == p || addr > 0xFFFF) return 0;

    return (uint16_t)addr;
}

/* ── GET /api/v1/health ─── (no auth required) ─────────────────────── */
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
    if (!check_bearer_auth(req)) return send_unauthorized(req);

    cJSON *root = cJSON_CreateArray();
    if (!root) { httpd_resp_send_500(req); return ESP_FAIL; }

    int count = zigbee_coordinator_get_node_count();
    for (int i = 0; i < count; i++) {
        agri_node_info_t info;
        if (zigbee_coordinator_get_node_info(i, &info) == ESP_OK) {
            cJSON *node = cJSON_CreateObject();
            cJSON_AddStringToObject(node, "device_id",   info.device_id);
            cJSON_AddNumberToObject(node, "status",      (int)info.status);
            cJSON_AddStringToObject(node, "fw_ver",      info.fw_ver);
            cJSON_AddNumberToObject(node, "batt_mv",     info.batt_mv);
            cJSON_AddNumberToObject(node, "rssi_dbm",    info.rssi_dbm);
            cJSON_AddNumberToObject(node, "lqi",         info.lqi);
            cJSON_AddNumberToObject(node, "zigbee_addr", info.zigbee_short_addr);
            cJSON_AddStringToObject(node, "last_seen",   info.last_seen);
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

/* ── POST /api/v1/nodes/<addr>/cmd ────────────────────────────────── */
static esp_err_t handler_node_cmd(httpd_req_t *req)
{
    if (!check_bearer_auth(req)) return send_unauthorized(req);

    /* FIX 8a: correct address extraction */
    uint16_t addr = extract_node_addr(req->uri);
    if (addr == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid node address in URI");
        return ESP_FAIL;
    }

    char buf[512] = {0};
    int received  = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    agri_cmd_t cmd;
    if (agri_cmd_from_json(buf, &cmd) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
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

/* ── GET /api/v1/config — read live NVS values ─────────────────────── */
static esp_err_t handler_config_get(httpd_req_t *req)
{
    if (!check_bearer_auth(req)) return send_unauthorized(req);

    /* FIX 8c: read from NVS */
    char farm_id[32]    = {0};
    char ntp_server[64] = {0};
    char ota_url[128]   = {0};
    uint32_t sleep_sec  = 30;

    gw_nvs_config_get_str(GW_NVS_FARM_ID,    farm_id,    sizeof(farm_id),    "farm01");
    gw_nvs_config_get_str(GW_NVS_NTP_SERVER, ntp_server, sizeof(ntp_server), "pool.ntp.org");
    gw_nvs_config_get_str(GW_NVS_OTA_URL,    ota_url,    sizeof(ota_url),    "");
    gw_nvs_config_get_u32("sleep_sec",        &sleep_sec, 30);

    cJSON *root = cJSON_CreateObject();
    if (!root) { httpd_resp_send_500(req); return ESP_FAIL; }

    cJSON_AddStringToObject(root, "farm_id",    farm_id);
    cJSON_AddStringToObject(root, "ntp_server", ntp_server);
    cJSON_AddStringToObject(root, "ota_url",    ota_url);
    cJSON_AddNumberToObject(root, "sleep_sec",  (double)sleep_sec);

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

/* ── Route Registration ────────────────────────────────────────────── */
esp_err_t rest_api_start(void)
{
    httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
    config.server_port      = REST_PORT;
    config.max_uri_handlers = 12;
    config.stack_size       = 6144;
    config.uri_match_fn     = httpd_uri_match_wildcard;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    httpd_uri_t routes[] = {
        { .uri = "/api/v1/health",    .method = HTTP_GET,  .handler = handler_health    },
        { .uri = "/api/v1/nodes",     .method = HTTP_GET,  .handler = handler_nodes     },
        { .uri = "/api/v1/nodes/*",   .method = HTTP_POST, .handler = handler_node_cmd  },
        { .uri = "/api/v1/config",    .method = HTTP_GET,  .handler = handler_config_get},
    };

    for (int i = 0; i < (int)(sizeof(routes) / sizeof(routes[0])); i++) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }

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
