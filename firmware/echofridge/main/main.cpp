#include <math.h>
#include <stdio.h>

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
#include "u8g2_st7305.h"
#include "user_config.h"

static const char *TAG = "echofridge";

static u8g2_st7305_t g_lcd;
static I2cMasterBus *g_i2c_bus = nullptr;
static Shtc3Port *g_shtc3 = nullptr;

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

// === 辅助函数：居中绘制字符串 ===
static void DrawStrCentered(u8g2_t *u8g2, int center_x, int y, const char *text)
{
    int width = (int)u8g2_GetUTF8Width(u8g2, text);
    u8g2_DrawUTF8(u8g2, center_x - width / 2, y, text);
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

// === 核心修改部分：绘制骨架 + 时间 ===
static void DrawUI(u8g2_t *u8g2, const rtcTimeStruct_t &time, float temp_c, float humidity)
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
    int radius = 10;        // 外圆角半径
    int inner_offset = 4;  // 内部黑线的缩进距离

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

    // 3. 绘制面板内部的极细黑线（内边框）
    u8g2_SetDrawColor(u8g2, 0); // 切换为黑色，用于画线和写字
    u8g2_DrawRFrame(u8g2, x1 + inner_offset, y1 + inner_offset, w1 - inner_offset * 2, h1 - inner_offset * 2, radius - 2);
    u8g2_DrawRFrame(u8g2, x2 + inner_offset, y2 + inner_offset, w2 - inner_offset * 2, h2 - inner_offset * 2, radius - 2);
    u8g2_DrawRFrame(u8g2, x3 + inner_offset, y3 + inner_offset, w3 - inner_offset * 2, h3 - inner_offset * 2, radius - 2);
    u8g2_DrawRFrame(u8g2, x4 + inner_offset, y4 + inner_offset, w4 - inner_offset * 2, h4 - inner_offset * 2, radius - 2);

    // --------------------------------------------------
    // 4. 填充内容：左上角时间
    // --------------------------------------------------
    char time_str[12];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", time.hour, time.minute);
    
    // 设置字体模式为透明背景
    u8g2_SetFontMode(u8g2, 1); 
    // 使用从 assets/fonts/Anton-Regular.ttf 转换出来的 U8G2 字体。
    u8g2_SetFont(u8g2, u8g2_font_anton_90_time);
    
    // 计算文字在面板1中的居中坐标
    int center_x1 = x1 + w1 / 2;
    // u8g2 的 Y 坐标是文字的基线 (Baseline)，这里让时间在框内偏上。
    int baseline_y1 = y1 + 92;
    
    DrawStrCentered(u8g2, center_x1, baseline_y1, time_str);

    // 时间下方的分割线，线下方显示温湿度摘要。
    int divider_y = y1 + h1 - 44;
    u8g2_DrawHLine(u8g2, x1 + inner_offset + 12, divider_y, w1 - (inner_offset + 12) * 2);
    DrawClimateSummary(u8g2, center_x1, y1 + h1 - 12, temp_c, humidity);

    // 左下角照片位图：贴在内边框线以内，内边框就是照片的边缘。
    int photo_x = x3 + inner_offset + 1;
    int photo_y = y3 + inner_offset + 1;
    u8g2_DrawXBMP(u8g2, photo_x, photo_y, kCatPhotoWidth, kCatPhotoHeight, cat_photo_bits);
    u8g2_DrawRFrame(u8g2, x3 + inner_offset, y3 + inner_offset, w3 - inner_offset * 2, h3 - inner_offset * 2, radius - 2);

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

    u8g2_t *u8g2 = u8g2_st7305_get_u8g2(&g_lcd);
    float temp_c = NAN;
    float humidity = NAN;
    int64_t last_sensor_read_us = 0;

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

        rtcTimeStruct_t time = {};
        Rtc_GetTime(&time);
        if (!RtcTimeLooksValid(time)) {
            SetRtcFromBuildTime();
            Rtc_GetTime(&time);
        }

        // 调用 UI 绘制函数，并传入当前时间、温度和湿度
        DrawUI(u8g2, time, temp_c, humidity);
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
