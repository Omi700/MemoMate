#include "sd_card.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "user_config.h"

static const char *TAG = "sdcard";
static sdmmc_card_t *g_card = nullptr;
static bool g_sdcard_mounted = false;

extern const uint8_t default_poems_json_start[] asm("_binary_poems_json_start");
extern const uint8_t default_poems_json_end[] asm("_binary_poems_json_end");

esp_err_t InitSdCard()
{
    if (g_sdcard_mounted) {
        return ESP_OK;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 16 * 1024;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = SDCARD_BUS_WIDTH;
    slot_config.clk = SDCARD_CLK_PIN;
    slot_config.cmd = SDCARD_CMD_PIN;
    slot_config.d0 = SDCARD_D0_PIN;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Mounting TF card at %s", SDCARD_MOUNT_POINT);
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SDCARD_MOUNT_POINT, &host, &slot_config,
                                            &mount_config, &g_card);
    if (ret != ESP_OK) {
        g_card = nullptr;
        g_sdcard_mounted = false;
        ESP_LOGW(TAG, "TF card mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    g_sdcard_mounted = true;
    sdmmc_card_print_info(stdout, g_card);
    return ESP_OK;
}

bool IsSdCardMounted()
{
    return g_sdcard_mounted;
}

void LogSdCardRoot()
{
    if (!g_sdcard_mounted) {
        ESP_LOGW(TAG, "TF card is not mounted, skip listing root");
        return;
    }

    DIR *dir = opendir(SDCARD_MOUNT_POINT);
    if (dir == nullptr) {
        ESP_LOGW(TAG, "Open %s failed: errno=%d", SDCARD_MOUNT_POINT, errno);
        return;
    }

    ESP_LOGI(TAG, "Root files:");
    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        ESP_LOGI(TAG, "  %s", entry->d_name);
    }
    closedir(dir);
}

void ProbeRemindersFile()
{
    if (!g_sdcard_mounted) {
        return;
    }

    const char *path = SDCARD_MOUNT_POINT "/reminders.json";
    FILE *file = fopen(path, "rb");
    if (file == nullptr) {
        ESP_LOGI(TAG, "No reminders file yet: %s", path);
        return;
    }

    char preview[256] = {};
    size_t read_len = fread(preview, 1, sizeof(preview) - 1, file);
    fclose(file);

    preview[read_len] = '\0';
    ESP_LOGI(TAG, "reminders.json preview (%u bytes): %s", (unsigned)read_len, preview);
}

esp_err_t EnsureDefaultPoemsFile()
{
    if (!g_sdcard_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *path = SDCARD_MOUNT_POINT "/poems.json";
    FILE *existing = fopen(path, "rb");
    if (existing != nullptr) {
        fclose(existing);
        ESP_LOGI(TAG, "poems.json already exists, keep TF card version");
        return ESP_OK;
    }

    size_t data_len = default_poems_json_end - default_poems_json_start;
    FILE *file = fopen(path, "wb");
    if (file == nullptr) {
        ESP_LOGW(TAG, "Open %s for writing failed: errno=%d", path, errno);
        return ESP_FAIL;
    }

    size_t written = fwrite(default_poems_json_start, 1, data_len, file);
    fclose(file);

    if (written != data_len) {
        ESP_LOGW(TAG, "Write poems.json failed (%u/%u bytes)", (unsigned)written, (unsigned)data_len);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Initialized %s from embedded default (%u bytes)", path, (unsigned)data_len);
    return ESP_OK;
}

void ProbePoemsFile()
{
    if (!g_sdcard_mounted) {
        return;
    }

    const char *path = SDCARD_MOUNT_POINT "/poems.json";
    FILE *file = fopen(path, "rb");
    if (file == nullptr) {
        ESP_LOGI(TAG, "No poems file yet: %s", path);
        return;
    }

    char preview[256] = {};
    size_t read_len = fread(preview, 1, sizeof(preview) - 1, file);
    fclose(file);

    preview[read_len] = '\0';
    ESP_LOGI(TAG, "poems.json preview (%u bytes): %s", (unsigned)read_len, preview);
}
