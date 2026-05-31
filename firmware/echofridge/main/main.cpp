#include <math.h>
#include <stdio.h>
#include <string.h>

#include "build_time.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "fonts/anton_fonts.h"
#include "i2c_bsp.h"
#include "i2c_equipment.h"
#include "icons/climate_icons.h"
#include "images/cat_photo.h"
#include "poem_data.h"
#include "sd_card.h"
#include "u8g2_st7305.h"
#include "user_config.h"

static const char *TAG = "echofridge";

static u8g2_st7305_t g_lcd;
static I2cMasterBus *g_i2c_bus = nullptr;
static Shtc3Port *g_shtc3 = nullptr;

// static const int64_t kPoemRefreshIntervalUs = 30LL * 1000 * 1000; // 测试阶段：30 秒刷新一次，后续可改成 2 小时。
static const int64_t kPoemRefreshIntervalUs = 2LL * 60 * 60 * 1000 * 1000;

// 检查时间是否有效
static bool RtcTimeLooksValid(const rtcTimeStruct_t &time)
{
    return time.year >= 2024 && time.year <= 2099 &&
           time.month >= 1 && time.month <= 12 &&
           time.day >= 1 && time.day <= 31 &&
           time.hour >= 0 && time.hour <= 23 &&
           time.minute >= 0 && time.minute <= 59 &&
           time.second >= 0 && time.second <= 59;
}

// 写入编译时间
static void SetRtcFromBuildTime()
{
    ESP_LOGI(TAG, "Build timestamp: %04d-%02d-%02d %02d:%02d:%02d",
             BUILD_YEAR, BUILD_MONTH, BUILD_DAY, BUILD_HOUR, BUILD_MINUTE, BUILD_SECOND);
    Rtc_SetTime((uint16_t)BUILD_YEAR, (uint8_t)BUILD_MONTH, (uint8_t)BUILD_DAY,
                (uint8_t)BUILD_HOUR, (uint8_t)BUILD_MINUTE, (uint8_t)BUILD_SECOND);
}

// === 辅助函数：把月份数字转换成英文缩写 ===
static const char *GetMonthName(int month)
{
    static const char *names[] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
    };
    if (month < 1 || month > 12) {
        return "---";
    }
    return names[month - 1];
}

// === 辅助函数：居中绘制字符串 ===
static void DrawStrCentered(u8g2_t *u8g2, int center_x, int y, const char *text)
{
    int width = (int)u8g2_GetUTF8Width(u8g2, text);
    u8g2_DrawUTF8(u8g2, center_x - width / 2, y, text);
}

// 取 UTF-8 字符串前 N 个字符的显示宽度，用来做中文诗句的字符级对齐。
static int GetUtf8PrefixWidth(u8g2_t *u8g2, const char *text, int char_count)
{
    char prefix[64];
    int bytes = 0;
    int chars = 0;

    while (text[bytes] != '\0' && chars < char_count) {
        unsigned char c = (unsigned char)text[bytes];
        int step = 1;
        if ((c & 0xE0) == 0xC0) {
            step = 2;
        } else if ((c & 0xF0) == 0xE0) {
            step = 3;
        } else if ((c & 0xF8) == 0xF0) {
            step = 4;
        }
        bytes += step;
        chars++;
    }

    if (bytes >= (int)sizeof(prefix)) {
        bytes = sizeof(prefix) - 1;
    }
    memcpy(prefix, text, bytes);
    prefix[bytes] = '\0';
    return (int)u8g2_GetUTF8Width(u8g2, prefix);
}

// === 绘制左上角时间卡片底部的温湿度摘要 ===
static void DrawClimateSummary(u8g2_t *u8g2, int center_x, int baseline_y, float temp_c, float humidity)
{
    char temp_text[16];
    char humidity_text[16];

    if (isnan(temp_c) || isnan(humidity)) {
        snprintf(temp_text, sizeof(temp_text), "--.-°C");
        snprintf(humidity_text, sizeof(humidity_text), "--%%");
    } else {
        snprintf(temp_text, sizeof(temp_text), "%.1f°C", temp_c);
        snprintf(humidity_text, sizeof(humidity_text), "%.0f%%", humidity);
    }

    u8g2_SetFont(u8g2, u8g2_font_montserrat_24_climate);

    int icon_text_gap = 4;
    int group_gap = 18;
    int temp_width = kClimateIconWidth + icon_text_gap + (int)u8g2_GetUTF8Width(u8g2, temp_text);
    int humidity_width = kClimateIconWidth + icon_text_gap + (int)u8g2_GetUTF8Width(u8g2, humidity_text);
    int total_width = temp_width + group_gap + humidity_width;
    int x = center_x - total_width / 2;
    int icon_y = baseline_y - kClimateIconHeight + 1;

    u8g2_DrawXBMP(u8g2, x, icon_y, kClimateIconWidth, kClimateIconHeight, temperature_icon_bits);
    u8g2_DrawUTF8(u8g2, x + kClimateIconWidth + icon_text_gap, baseline_y, temp_text);

    x += temp_width + group_gap;
    u8g2_DrawXBMP(u8g2, x, icon_y, kClimateIconWidth, kClimateIconHeight, humidity_icon_bits);
    u8g2_DrawUTF8(u8g2, x + kClimateIconWidth + icon_text_gap, baseline_y, humidity_text);
}

// === 绘制右上角日历面板 ===
static void DrawCalendarPanel(u8g2_t *u8g2, int ix, int iy, int iw, int ih, int radius, const rtcTimeStruct_t &time)
{
    int header_h = 36; // 顶部黑色横幅的高度

    // 1. 绘制顶部的黑色横幅 (反色区域)
    u8g2_SetDrawColor(u8g2, 0); // 黑色
    // 画一个上半部分带圆角的实心黑框
    u8g2_DrawRBox(u8g2, ix, iy, iw, header_h, radius); 
    // 把横幅下半部分的圆角填平，使其与下方白底无缝衔接
    u8g2_DrawBox(u8g2, ix, iy + radius, iw, header_h - radius); 

    // 2. 绘制活页扣 (环)
    int hook_w = 12;
    int hook_h = 10;
    int hook_y = iy - 4; // 稍微超出内边框一点点
    int hook1_x = ix + iw / 4 - hook_w / 2;
    int hook2_x = ix + iw * 3 / 4 - hook_w / 2;

    // 画扣子的黑边
    u8g2_SetDrawColor(u8g2, 0);
    u8g2_DrawRFrame(u8g2, hook1_x, hook_y, hook_w, hook_h, 2);
    u8g2_DrawRFrame(u8g2, hook2_x, hook_y, hook_w, hook_h, 2);
    // 填白底
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_DrawRBox(u8g2, hook1_x + 1, hook_y + 1, hook_w - 2, hook_h - 2, 1);
    u8g2_DrawRBox(u8g2, hook2_x + 1, hook_y + 1, hook_w - 2, hook_h - 2, 1);
    // 画扣子中间的小黑洞
    u8g2_SetDrawColor(u8g2, 0);
    u8g2_DrawBox(u8g2, hook1_x + hook_w / 2 - 1, hook_y + hook_h - 4, 3, 3);
    u8g2_DrawBox(u8g2, hook2_x + hook_w / 2 - 1, hook_y + hook_h - 4, 3, 3);

    // 3. 写入月份缩写 (黑底白字)
    u8g2_SetDrawColor(u8g2, 1); // 白色
    u8g2_SetFont(u8g2, u8g2_font_montserrat_21_month);
    const char *month_str = GetMonthName(time.month);
    DrawStrCentered(u8g2, ix + iw / 2, iy + 26, month_str);

    // 4. 写入巨大的日期 (白底黑字)
    u8g2_SetDrawColor(u8g2, 0); // 黑色
    // 使用和时间一致的 Anton 数字风格。
    u8g2_SetFont(u8g2, u8g2_font_anton_86_time);
    char date_str[4];
    snprintf(date_str, sizeof(date_str), "%d", time.day);
    
    // 用字体真实高度计算日期在下方白色区域的垂直居中基线。
    int remaining_h = ih - header_h;
    int date_area_y = iy + header_h;
    int font_ascent = u8g2_GetAscent(u8g2);
    int font_descent = u8g2_GetDescent(u8g2);
    int font_h = font_ascent - font_descent;
    int date_baseline_y = date_area_y + (remaining_h - font_h) / 2 + font_ascent + 4;
    DrawStrCentered(u8g2, ix + iw / 2, date_baseline_y, date_str);
}

// === 绘制右下角诗词面板 ===
static void DrawPoemPanel(u8g2_t *u8g2, int ix, int iy, int iw, const PoemDisplayData &poem)
{
    if (!poem.loaded) {
        return;
    }

    int line1_y = iy + 25;
    int line2_y = iy + 47;
    int meta_y = iy + 67;

    u8g2_SetDrawColor(u8g2, 0);
    u8g2_SetFontMode(u8g2, 1);

    // 三行作为一个整体居中，第二行保持相对第一行第六个字的缩进。
    u8g2_SetFont(u8g2, u8g2_font_lxgw_wenkai_17_poem);
    int line2_offset = GetUtf8PrefixWidth(u8g2, poem.line1, 5);
    int line1_width = (int)u8g2_GetUTF8Width(u8g2, poem.line1);
    int line2_width = (int)u8g2_GetUTF8Width(u8g2, poem.line2);

    u8g2_SetFont(u8g2, u8g2_font_lxgw_wenkai_14_poem);
    int meta_width = (int)u8g2_GetUTF8Width(u8g2, poem.meta);

    int line2_end = line2_offset + line2_width;
    int meta_offset = line2_end - meta_width;
    int block_left = meta_offset < 0 ? meta_offset : 0;
    int block_right = line1_width > line2_end ? line1_width : line2_end;
    int block_width = block_right - block_left;
    int block_x = ix + (iw - block_width) / 2 - block_left;

    u8g2_SetFont(u8g2, u8g2_font_lxgw_wenkai_17_poem);
    int line1_x = block_x;
    u8g2_DrawUTF8(u8g2, line1_x, line1_y, poem.line1);

    // 第二行：第一个字与第一行第六个字对齐。
    int line2_x = block_x + line2_offset;
    u8g2_DrawUTF8(u8g2, line2_x, line2_y, poem.line2);

    // 第三行：最后一个字与第二行最后一个字对齐。
    u8g2_SetFont(u8g2, u8g2_font_lxgw_wenkai_14_poem);
    u8g2_DrawUTF8(u8g2, block_x + meta_offset, meta_y, poem.meta);
}

// === 核心界面绘制 ===
static void DrawUI(u8g2_t *u8g2, const rtcTimeStruct_t &time, float temp_c, float humidity, const PoemDisplayData &poem)
{
    // 清除缓冲区
    u8g2_ClearBuffer(u8g2);

    // 1. 绘制全黑背景
    u8g2_SetDrawColor(u8g2, 0);
    u8g2_DrawBox(u8g2, 0, 0, LCD_WIDTH, LCD_HEIGHT); 

    // --------------------------------------------------
    // 【布局参数微调区】
    // --------------------------------------------------
    int gap = 4;           // 面板之间的黑边间距
    int margin = 6;        // 屏幕边缘的留白
    int radius = 10;       // 外圆角半径
    int inner_offset = 4;  // 内部黑线的缩进距离
    int r_inner = radius - 2; // 内框的圆角半径

    // 动态计算纯面板可用的总宽度和总高度
    int available_w = LCD_WIDTH - 2 * margin - gap;
    int available_h = LCD_HEIGHT - 2 * margin - gap;

    // 左上：时间 
    int w1 = available_w * 263 / 388; 
    int h1 = available_h * 152 / 288;
    int x1 = margin;
    int y1 = margin;

    // 右上：日历 
    int w2 = available_w - w1; 
    int h2 = h1; 
    int x2 = x1 + w1 + gap;
    int y2 = margin;

    // 左下：插画
    int w3 = available_w * 146 / 388; 
    int h3 = available_h - h1; 
    int x3 = margin;
    int y3 = y1 + h1 + gap;

    // 右下：音乐
    int w4 = available_w - w3; 
    int h4 = h3;  
    int x4 = x3 + w3 + gap;
    int y4 = y3;

    // 2. 绘制 4 个白色的实心面板（白底）
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_DrawRBox(u8g2, x1, y1, w1, h1, radius);
    u8g2_DrawRBox(u8g2, x2, y2, w2, h2, radius);
    u8g2_DrawRBox(u8g2, x3, y3, w3, h3, radius);
    u8g2_DrawRBox(u8g2, x4, y4, w4, h4, radius);

    // 3. 绘制内部黑边框，并填充各区域内容
    u8g2_SetDrawColor(u8g2, 0); // 恢复黑笔

    // --- 左上角：时间与温湿度 ---
    u8g2_DrawRFrame(u8g2, x1 + inner_offset, y1 + inner_offset, w1 - inner_offset * 2, h1 - inner_offset * 2, r_inner);
    char time_str[12];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", time.hour, time.minute);
    u8g2_SetFontMode(u8g2, 1); 
    u8g2_SetFont(u8g2, u8g2_font_anton_92_time);
    int divider_y = y1 + h1 - 44;
    int time_area_y = y1 + inner_offset;
    int time_area_h = divider_y - time_area_y;
    int time_font_ascent = u8g2_GetAscent(u8g2);
    int time_font_descent = u8g2_GetDescent(u8g2);
    int time_font_h = time_font_ascent - time_font_descent;
    // Anton 字体的可见字形比字体指标略偏上，额外下移让视觉中心落在内边框和横线之间。
    int time_baseline_y = time_area_y + (time_area_h - time_font_h) / 2 + time_font_ascent + 8;
    DrawStrCentered(u8g2, x1 + w1 / 2, time_baseline_y, time_str);
    u8g2_DrawHLine(u8g2, x1 + inner_offset + 12, divider_y, w1 - (inner_offset + 12) * 2);
    DrawClimateSummary(u8g2, x1 + w1 / 2, y1 + h1 - 12, temp_c, humidity);

    // --- 左下角：猫猫照片 ---
    int photo_x = x3 + inner_offset + 1;
    int photo_y = y3 + inner_offset + 1;
    u8g2_DrawXBMP(u8g2, photo_x, photo_y, kCatPhotoWidth, kCatPhotoHeight, cat_photo_bits);
    u8g2_DrawRFrame(u8g2, x3 + inner_offset, y3 + inner_offset, w3 - inner_offset * 2, h3 - inner_offset * 2, r_inner);

    // --- 右上角：日历 ---
    // 首先画日历的基础内框，然后调用专门的日历绘制函数覆盖内部
    u8g2_DrawRFrame(u8g2, x2 + inner_offset, y2 + inner_offset, w2 - inner_offset * 2, h2 - inner_offset * 2, r_inner);
    DrawCalendarPanel(u8g2, x2 + inner_offset, y2 + inner_offset, w2 - inner_offset * 2, h2 - inner_offset * 2, r_inner, time);

    // --- 右下角：留给音乐播放器 ---
    u8g2_SetDrawColor(u8g2, 0); // 确保笔刷是黑色
    u8g2_DrawRFrame(u8g2, x4 + inner_offset, y4 + inner_offset, w4 - inner_offset * 2, h4 - inner_offset * 2, r_inner);
    DrawPoemPanel(u8g2, x4 + inner_offset, y4 + inner_offset, w4 - inner_offset * 2, poem);

    // 发送到屏幕
    u8g2_SendBuffer(u8g2);
}

// 初始化屏幕
static esp_err_t InitDisplay()
{
    u8g2_st7305_config_t config = u8g2_st7305_default_config();
    config.mosi_io = RLCD_MOSI_PIN;
    config.sclk_io = RLCD_SCK_PIN;
    config.dc_io = RLCD_DC_PIN;
    config.cs_io = RLCD_CS_PIN;
    config.reset_io = RLCD_RST_PIN;
    config.rotation = U8G2_R1;
    config.tile_buf_height = U8G2_ST7305_TILE_BUF_FULL;
    config.prefer_psram = true;
    return u8g2_st7305_init(&g_lcd, &config);
}

// 初始化传感器
static void InitSensors()
{
    g_i2c_bus = new I2cMasterBus(ESP32_I2C_SCL_PIN, ESP32_I2C_SDA_PIN, ESP32_I2C_PORT);
    Rtc_Setup(g_i2c_bus, PCF85063_ADDR);

    ESP_LOGI(TAG, "Syncing RTC from firmware build timestamp");
    SetRtcFromBuildTime();

    g_shtc3 = new Shtc3Port(*g_i2c_bus);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "EchoFridge dashboard starting");
    ESP_ERROR_CHECK(InitDisplay());
    InitSensors();
    PoemDisplayData poem = {};
    if (InitSdCard() == ESP_OK) {
        EnsureDefaultPoemsFile();
        LogSdCardRoot();
        ProbePoemsFile();
        ProbeRemindersFile();
        LoadRandomPoemFromSd(&poem);
    }

    u8g2_t *u8g2 = u8g2_st7305_get_u8g2(&g_lcd);
    float temp_c = NAN;
    float humidity = NAN;
    int64_t last_sensor_read_us = 0;
    int64_t last_poem_refresh_us = esp_timer_get_time();

    while (true) {
        int64_t now_us = esp_timer_get_time();
        if (now_us - last_sensor_read_us > 5000000 || isnan(temp_c) || isnan(humidity)) {
            float next_temp = NAN;
            float next_humidity = NAN;
            if (g_shtc3->Shtc3_ReadTempHumi(&next_temp, &next_humidity) == 0) {
                temp_c = next_temp;
                humidity = next_humidity;
            } else {
                ESP_LOGW(TAG, "SHTC3 read failed");
            }
            last_sensor_read_us = now_us;
        }

        if (now_us - last_poem_refresh_us >= kPoemRefreshIntervalUs) {
            PoemDisplayData next_poem = {};
            if (LoadRandomPoemFromSd(&next_poem) == ESP_OK) {
                poem = next_poem;
            }
            last_poem_refresh_us = now_us;
        }

        rtcTimeStruct_t time = {};
        Rtc_GetTime(&time);
        if (!RtcTimeLooksValid(time)) {
            SetRtcFromBuildTime();
            Rtc_GetTime(&time);
        }

        // 调用 UI 绘制函数，并传入当前时间、温度和湿度
        DrawUI(u8g2, time, temp_c, humidity, poem);
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
