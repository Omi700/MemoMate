# EchoFridge Firmware

这是给微雪 ESP32-S3-RLCD-4.2 做的第一版独立固件项目，目标是先把核心常驻界面跑起来：时间、日期、室内温度、室内湿度。

## 当前 UI 方案

当前界面走「复古电子纸信息牌」路线，先保证清晰、稳定、低复杂度：

- 左上：大号 `HH:MM` 时间。
- 右上：月份缩写和日期。
- 左下：像素插画。
- 右下：温度和湿度。

现在先用英文标签，主要是为了减少中文字体体积和字库处理成本。等功能稳定后，可以再做中文字体、像素图标、提醒状态和更精致的视觉版本。

## 已接入硬件

- 屏幕：ST7305 RLCD，使用官方 U8G2 驱动。
- 温湿度：板载 SHTC3，通过 I2C 读取。
- RTC：板载 PCF85063，通过 I2C 读取时间。
- I2C 引脚：`SCL = GPIO14`，`SDA = GPIO13`。

如果 RTC 时间无效，固件会先用编译时间写入 RTC，后面可以改成联网后自动 NTP 校时。
当前开发阶段为了方便调试，固件每次启动都会用本次编译时间同步 RTC；正式版会改成 Wi-Fi/NTP 自动校时。

## 目录

- `main/main.cpp`：主界面、RTC、温湿度读取逻辑。
- `main/user_config.h`：屏幕和 I2C 引脚配置。
- `components/u8g2`：官方 U8G2 组件。
- `components/u8g2_st7305`：官方 ST7305 屏幕适配组件。
- `components/port_bsp`：I2C、SHTC3、PCF85063 适配层。
- `components/ExternLib/SensorLib`：官方传感器库。

## 编译

在项目目录执行：

```bash
cd /Users/fu70/Documents/EchoFridge/firmware/echofridge
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
. /Users/fu70/esp/esp-idf-v5.5.1/export.sh
idf.py build
```

我已经验证过当前版本可以编译通过。

## 刷机

确认要覆盖板子当前固件后再执行：

```bash
cd /Users/fu70/Documents/EchoFridge/firmware/echofridge
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
. /Users/fu70/esp/esp-idf-v5.5.1/export.sh
idf.py -p /dev/cu.usbmodem101 flash monitor
```

退出串口监视器用 `Ctrl+]`。

## 下一步路线

1. 先刷机确认屏幕方向、时间位置、温湿度数据是否正常。
2. 加 Wi-Fi 配网和 NTP 校时，让时间自动准确。
3. 加提醒数据结构，先用本地内置提醒验证播报流程。
4. 再做手机端设置方式：优先考虑微信小程序或一个简单网页后台。
5. 最后做视觉精修：中文字体、提醒卡片、低电量状态、充电状态。
