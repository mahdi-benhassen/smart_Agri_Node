/*
 * cloud_buffer.c — SPIFFS ring-buffer for offline telemetry storage (72h max)
 *
 * FIXES applied:
 *  1. Indices persisted to NVS so buffered entries survive a reboot.
 *  2. Overflow advances the read pointer (overwrites oldest), honouring the
 *     "overwrite oldest" policy stated in the spec.
 *  3. All index mutations protected by a FreeRTOS mutex for thread-safety.
 */
#include "cloud_buffer.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "CLOUD_BUF";

#define SPIFFS_BASE_PATH     "/spiffs"
#define MAX_FILENAME_LEN     64
#define NVS_CB_NAMESPACE     "cloud_buf"
#define NVS_KEY_WRITE_IDX    "wr_idx"
#define NVS_KEY_READ_IDX     "rd_idx"

static uint32_t           s_write_idx  = 0;
static uint32_t           s_read_idx   = 0;
static bool               s_initialized = false;
static SemaphoreHandle_t  s_mutex       = NULL;
static nvs_handle_t       s_nvs         = 0;

/* ── Internal helpers ──────────────────────────────────────────────── */

static void get_filename(uint32_t idx, char *buf, size_t len)
{
    snprintf(buf, len, SPIFFS_BASE_PATH "/b%08lu.json",
             (unsigned long)(idx % CLOUD_BUFFER_MAX_ENTRIES));
}

/** Persist both indices to NVS so they survive a reboot. */
static void save_indices(void)
{
    if (s_nvs == 0) return;
    nvs_set_u32(s_nvs, NVS_KEY_WRITE_IDX, s_write_idx);
    nvs_set_u32(s_nvs, NVS_KEY_READ_IDX,  s_read_idx);
    nvs_commit(s_nvs);
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t cloud_buffer_init(void)
{
    /* Create mutex */
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex create failed");
        return ESP_ERR_NO_MEM;
    }

    /* Open / create NVS namespace to persist indices */
    esp_err_t ret = nvs_open(NVS_CB_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed (%s) — indices will not persist across reboots",
                 esp_err_to_name(ret));
        s_nvs = 0;
    } else {
        /* Restore indices from NVS */
        nvs_get_u32(s_nvs, NVS_KEY_WRITE_IDX, &s_write_idx);
        nvs_get_u32(s_nvs, NVS_KEY_READ_IDX,  &s_read_idx);
        ESP_LOGI(TAG, "Restored indices: rd=%lu wr=%lu",
                 (unsigned long)s_read_idx, (unsigned long)s_write_idx);
    }

    /* Mount SPIFFS */
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = SPIFFS_BASE_PATH,
        .partition_label        = "spiffs",
        .max_files              = 12,
        .format_if_mount_failed = true,
    };

    ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info("spiffs", &total, &used);
    ESP_LOGI(TAG, "SPIFFS: total=%zu used=%zu  buffered entries=%lu",
             total, used, (unsigned long)cloud_buffer_count());

    s_initialized = true;
    return ESP_OK;
}

esp_err_t cloud_buffer_write(const char *json)
{
    if (!s_initialized || !json) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Overflow: if full, advance read pointer to discard the oldest entry. */
    if ((s_write_idx - s_read_idx) >= CLOUD_BUFFER_MAX_ENTRIES) {
        char old_fname[MAX_FILENAME_LEN];
        get_filename(s_read_idx, old_fname, sizeof(old_fname));
        remove(old_fname);          /* Best-effort delete of overwritten slot */
        s_read_idx++;
        ESP_LOGW(TAG, "Buffer full — oldest entry discarded (rd_idx=%lu)",
                 (unsigned long)s_read_idx);
    }

    char fname[MAX_FILENAME_LEN];
    get_filename(s_write_idx, fname, sizeof(fname));

    FILE *f = fopen(fname, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", fname);
        xSemaphoreGive(s_mutex);
        return ESP_FAIL;
    }

    fputs(json, f);
    fclose(f);

    s_write_idx++;
    save_indices();

    ESP_LOGD(TAG, "Buffered entry wr=%lu (count=%lu)",
             (unsigned long)(s_write_idx - 1),
             (unsigned long)cloud_buffer_count());

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t cloud_buffer_read_next(char *buf, size_t max_len)
{
    if (!s_initialized || !buf || max_len == 0) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_read_idx >= s_write_idx) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    char fname[MAX_FILENAME_LEN];
    get_filename(s_read_idx, fname, sizeof(fname));

    FILE *f = fopen(fname, "r");
    if (!f) {
        /* File missing (e.g. formatted); skip this slot */
        ESP_LOGW(TAG, "Missing file %s — skipping", fname);
        s_read_idx++;
        save_indices();
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    size_t n = fread(buf, 1, max_len - 1, f);
    buf[n] = '\0';
    fclose(f);

    remove(fname);
    s_read_idx++;
    save_indices();

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

bool cloud_buffer_has_data(void)
{
    if (!s_initialized) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool has = (s_read_idx < s_write_idx);
    xSemaphoreGive(s_mutex);
    return has;
}

uint32_t cloud_buffer_count(void)
{
    if (!s_initialized) return 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint32_t c = (s_write_idx > s_read_idx) ? (s_write_idx - s_read_idx) : 0;
    xSemaphoreGive(s_mutex);
    return c;
}

esp_err_t cloud_buffer_clear(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Delete all remaining files */
    for (uint32_t i = s_read_idx; i < s_write_idx; i++) {
        char fname[MAX_FILENAME_LEN];
        get_filename(i, fname, sizeof(fname));
        remove(fname);
    }

    s_read_idx  = s_write_idx;
    save_indices();

    ESP_LOGI(TAG, "Buffer cleared");
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}
