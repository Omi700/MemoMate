#include "poem_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "user_config.h"

static const char *TAG = "poem";

static void CopyText(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static void StripTrailingPunctuation(char *text)
{
    const char *punctuation[] = {"。", "，", "？", "！", "?", "!", ",", nullptr};
    bool removed = true;
    while (removed) {
        removed = false;
        size_t len = strlen(text);
        for (int i = 0; punctuation[i] != nullptr; ++i) {
            size_t punct_len = strlen(punctuation[i]);
            if (len >= punct_len && strcmp(text + len - punct_len, punctuation[i]) == 0) {
                text[len - punct_len] = '\0';
                removed = true;
                break;
            }
        }
    }
}

static void SplitPoemLine(const char *line, char *line1, size_t line1_size, char *line2, size_t line2_size)
{
    line1[0] = '\0';
    line2[0] = '\0';
    if (line == nullptr) {
        return;
    }

    const char *separator = strstr(line, "，");
    if (separator == nullptr) {
        separator = strstr(line, "。");
    }

    if (separator == nullptr) {
        CopyText(line1, line1_size, line);
        StripTrailingPunctuation(line1);
        return;
    }

    size_t first_len = separator - line;
    if (first_len >= line1_size) {
        first_len = line1_size - 1;
    }
    memcpy(line1, line, first_len);
    line1[first_len] = '\0';
    StripTrailingPunctuation(line1);

    const char *second = separator + strlen("，");
    CopyText(line2, line2_size, second);
    StripTrailingPunctuation(line2);
}

esp_err_t LoadRandomPoemFromSd(PoemDisplayData *poem)
{
    if (poem == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    poem->loaded = false;
    poem->line1[0] = '\0';
    poem->line2[0] = '\0';
    poem->meta[0] = '\0';

    const char *path = SDCARD_MOUNT_POINT "/poems.json";
    FILE *file = fopen(path, "rb");
    if (file == nullptr) {
        ESP_LOGW(TAG, "Open %s failed", path);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size <= 0 || file_size > 128 * 1024) {
        fclose(file);
        ESP_LOGW(TAG, "Unexpected poems.json size: %ld", file_size);
        return ESP_FAIL;
    }

    char *json = (char *)malloc((size_t)file_size + 1);
    if (json == nullptr) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    size_t read_len = fread(json, 1, (size_t)file_size, file);
    fclose(file);
    json[read_len] = '\0';

    cJSON *root = cJSON_ParseWithLength(json, read_len);
    free(json);
    if (root == nullptr) {
        ESP_LOGW(TAG, "Parse poems.json failed");
        return ESP_FAIL;
    }

    int poem_count = cJSON_GetArraySize(root);
    if (poem_count <= 0) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "poems.json has no poems");
        return ESP_FAIL;
    }

    int poem_index = (int)(esp_random() % poem_count);
    cJSON *poem_item = cJSON_GetArrayItem(root, poem_index);
    cJSON *lines = cJSON_GetObjectItem(poem_item, "lines");
    cJSON *first_line = cJSON_GetArrayItem(lines, 0);
    cJSON *dynasty = cJSON_GetObjectItem(poem_item, "dynasty");
    cJSON *author = cJSON_GetObjectItem(poem_item, "author");
    cJSON *title = cJSON_GetObjectItem(poem_item, "title");

    if (!cJSON_IsString(first_line) || !cJSON_IsString(author)) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "poems.json missing required poem fields");
        return ESP_FAIL;
    }

    SplitPoemLine(first_line->valuestring, poem->line1, sizeof(poem->line1),
                  poem->line2, sizeof(poem->line2));
    if (cJSON_IsString(dynasty) && dynasty->valuestring[0] != '\0') {
        snprintf(poem->meta, sizeof(poem->meta), "—%s·%s", dynasty->valuestring, author->valuestring);
    } else {
        snprintf(poem->meta, sizeof(poem->meta), "—%s", author->valuestring);
    }

    poem->loaded = true;
    ESP_LOGI(TAG, "Loaded poem[%d/%d] %s: %s / %s %s",
             poem_index + 1,
             poem_count,
             cJSON_IsString(title) ? title->valuestring : "",
             poem->line1,
             poem->line2,
             poem->meta);
    cJSON_Delete(root);
    return ESP_OK;
}
