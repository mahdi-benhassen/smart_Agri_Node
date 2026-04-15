/*
 * cloud_buffer.c — SPIFFS ring-buffer for offline telemetry storage (72h max)
 */
#include "cloud_buffer.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "CLOUD_BUF";

#define SPIFFS_BASE_PATH    "/spiffs"
#define BUFFER_DIR          SPIFFS_BASE_PATH "/buf"
#define MAX_FILENAME_LEN    64

static uint32_t s_write_idx = 0;
static uint32_t s_read_idx  = 0;
static bool     s_initialized = false;

/* ── Internal: Get filename for index ──────────────────────────────── */
static void get_filename(uint32_t idx, char *buf, size_t len)
{
    snprintf(buf, len, SPIFFS_BASE_PATH "/b%08lu.json", (unsigned long)(idx % CLOUD_BUFFER_MAX_ENTRIES));
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t cloud_buffer_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = SPIFFS_BASE_PATH,
        .partition_label        = "spiffs",
        .max_files              = 10,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info("spiffs", &total, &used);
    ESP_LOGI(TAG, "SPIFFS: total=%zu used=%zu", total, used);

    s_write_idx = 0;
    s_read_idx  = 0;
    s_initialized = true;

    ESP_LOGI(TAG, "Cloud buffer initialized (max %d entries)", CLOUD_BUFFER_MAX_ENTRIES);
    return ESP_OK;
}

esp_err_t cloud_buffer_write(const char *json)
{
    if (!s_initialized || !json) return ESP_ERR_INVALID_STATE;

    char fname[MAX_FILENAME_LEN];
    get_filename(s_write_idx, fname, sizeof(fname));

    FILE *f = fopen(fname, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", fname);
        return ESP_FAIL;
    }

    fputs(json, f);
    fclose(f);

    s_write_idx++;
    ESP_LOGD(TAG, "Buffered entry #%lu", (unsigned long)s_write_idx);
    return ESP_OK;
}

esp_err_t cloud_buffer_read_next(char *buf, size_t max_len)
{
    if (!s_initialized || !buf || max_len == 0) return ESP_ERR_INVALID_ARG;

    if (s_read_idx >= s_write_idx) {
        return ESP_ERR_NOT_FOUND;  /* No data */
    }

    char fname[MAX_FILENAME_LEN];
    get_filename(s_read_idx, fname, sizeof(fname));

    FILE *f = fopen(fname, "r");
    if (!f) {
        s_read_idx++;
        return ESP_ERR_NOT_FOUND;
    }

    size_t read = fread(buf, 1, max_len - 1, f);
    buf[read] = '\0';
    fclose(f);

    /* Delete after reading */
    remove(fname);
    s_read_idx++;

    return ESP_OK;
}

bool cloud_buffer_has_data(void)
{
    return s_initialized && (s_read_idx < s_write_idx);
}

uint32_t cloud_buffer_count(void)
{
    if (!s_initialized || s_write_idx <= s_read_idx) return 0;
    return s_write_idx - s_read_idx;
}

esp_err_t cloud_buffer_clear(void)
{
    s_read_idx = s_write_idx;
    ESP_LOGI(TAG, "Buffer cleared");
    return ESP_OK;
}
