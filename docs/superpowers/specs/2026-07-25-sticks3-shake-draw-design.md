# StickS3 摇签遥控器设计（BLE 广播联动）

日期：2026-07-25
状态：已确认（用户批准方案 A：无连接 BLE 广播）

## 背景与目标

赛博财神庙当前由 T5AI-Board（TuyaOpen + LVGL，`t5-dev/TuyaOpen/apps/cyber_fortune`）承担触屏求签 UI 与 USB 打印。
新增硬件 M5Stack **StickS3**（SKU:K150）作为手持"签筒"：用户摇晃 StickS3 即触发大屏求签，
等价于点击屏幕上的「求签」按钮。

- 产品页：https://docs.m5stack.com/zh_CN/core/StickS3
- 商店页：https://shop.m5stack.com/products/m5sticks3-esp32s3-mini-iot-dev-kit

### 关键硬件事实

| 项 | StickS3 | T5AI-Board |
|---|---|---|
| 主控 | ESP32-S3-PICO-1-N8R8（8MB Flash / 8MB PSRAM） | Tuya T5（BK7258 系） |
| 无线 | 2.4G WiFi + **BLE 5.0（无经典蓝牙）** | WiFi + BLE（`tal_bluetooth`，有 `ble_central` 例程） |
| 传感器 | **BMI270 六轴 IMU**（I2C 0x68，摇动检测核心） | — |
| 交互 | 1.14" ST7789P3 135x240 LCD、扬声器（ES8311+AW8737）、双按键 | 3.5" ILI9488 480x320 触屏 |
| 供电 | 250mAh 电池 + USB Type-C | USB |
| 刷机 | Type-C 原生 USB CDC，PlatformIO 自动进 boot；手动方式：连 USB 长按侧边复位键至绿 LED 闪 | CH343（无自动复位） |

## 需求决策（已与用户确认）

1. **触发方式：两者并存** —— 触屏「求签」按钮保留，摇 StickS3 同样触发；Stick 离线不影响触屏流程。
2. **摇签时机：全局响应** —— 待机页摇动=求签；结果页摇动=等价「再求一签」并立即重摇；求签动画中忽略。
3. **技术栈**（用户委托决定）：StickS3 侧 PlatformIO + Arduino + M5Unified；连接采用**方案 A：无连接 BLE 广播**。

### 备选方案（已否决）

- 方案 B（GATT 长连接）：可双向通信但需断线重连状态机，展会 2.4G 干扰下稳定性差，工程量 2~3 倍。协议中预留事件类型字段，未来可升级。
- 方案 C（WiFi 局域网/后端中转）：依赖场地 WiFi 与配网，链路环节最多。

## 1. 通信协议

StickS3 检测到摇动后，将事件写入 BLE 广播包的 **Manufacturer Specific Data（AD Type 0xFF）**：

```
偏移  字段            值/说明
0-1   厂商ID          0xFF 0xFF（测试保留值）
2-3   魔数            0x43 0x46（"CF"，过滤无关广播）
4     协议版本        0x01
5     事件类型        0x01 = 摇签（0x02+ 预留）
6     事件序号        每次摇签 +1，uint8 滚动，用于去重
7     电量百分比      0-100，T5 侧暂不消费，预留
```

- 触发后以 **50ms 间隔连发 1.5 秒**广播，随后停止（连发抗丢包）。
- T5AI 侧记录上一事件序号，**序号变化才触发**，天然去重。
- 无配对、无连接状态：Stick 重启/换电/离场返回均零恢复成本。

## 2. StickS3 固件（新增）

目录：`stick-dev/cf_stick/`（与 `t5-dev` 平级），PlatformIO 工程，`platformio.ini`
采用官方推荐配置（`espressif32@6.12.0`、`esp32-s3-devkitc-1`、`qio_opi`、
`ARDUINO_USB_CDC_ON_BOOT=1`），依赖 M5Unified + M5PM1。

- **摇动检测**：BMI270 加速度模长阈值（约 2g）+ 短窗口内多峰判定；触发后 **2 秒冷却**防连触。
- **本地仪式感**（不依赖回传）：待机显示像素风"签筒"画面；摇动瞬间播放音效 + 切"求签中…"动画，约 1.5 秒后回待机；显示电量。
- **BLE**：ESP32 Arduino BLEAdvertising API 动态设置厂商数据、启停广播。
- **供电**：展会默认 USB/充电宝供电，250mAh 电池仅作短时移动缓冲。
- 配套脚本：`cf_stick_build.sh` / `cf_stick_flash.sh`（含 `pio device monitor` 提示），与现有 `cf_*` 工作流一致。

## 3. T5AI 侧改动（`apps/cyber_fortune`）

- 新增 `fortune_ble_remote.c/h`（约 150 行）：
  - `tal_ble_bt_init(TAL_BLE_ROLE_CENTRAL, cb)` + 常驻扫描（参考 `examples/ble/ble_central`）。
  - 扫描回调（非 LVGL 线程）：过滤魔数 → 校验事件类型/序号 → 置 `volatile` 触发标志。
- **线程安全**：`fortune_ui.c` 增加 **100ms LVGL 轮询 timer** 消费标志位；所有 UI 变更仍在 LVGL 线程内，零跨线程调用。
- `fortune_ui.c` 新增公开函数 `fortune_ui_trigger_draw()`（全局响应语义）：
  - 待机页 → 等价点击求签按钮；
  - 结果页 → 回待机并立即开始求签；
  - `s_drawing` 为真时忽略。
- `tuya_main.c`：UI 初始化完成后调用 `fortune_ble_remote_init()`；初始化失败仅打日志，触屏流程不受影响。
- `app_default.config`：启用蓝牙相关配置项（具体 Kconfig 符号实施时确认）。

## 4. 容错与边界

| 场景 | 行为 |
|---|---|
| Stick 没电/丢失/离场 | 触屏求签照常可用 |
| 广播包重复接收 | 事件序号去重，只触发一次 |
| 连续疯狂摇动 | Stick 侧 2s 冷却 + T5 侧 `s_drawing` 期间忽略 |
| T5 BLE 初始化失败 | 打日志降级，主流程不受影响 |
| 展会 2.4G 干扰 | 1.5s × 50ms 广播连发冗余抗丢包 |

## 5. 测试计划

1. **Stick 单侧**：串口日志验证摇动检测灵敏度与冷却；手机 nRF Connect 抓广播包核对协议字段。
2. **T5 单侧**：monitor 日志确认扫描、魔数过滤、序号去重。
3. **端到端**：摇 Stick → 大屏出签 → 打印机出票；结果页摇动 → 重新求签。
4. **鲁棒性**：冷却周期内连摇 10 次仅出 1 签；Stick 拔电重启后无需任何操作继续可用。

## 假设

- StickS3 到手后 BMI270 阈值需实测微调（阈值/峰数作为固件顶部常量便于调参）。
- T5AI 的 BLE 与现有 WiFi/后端请求共存无冲突（TuyaOpen 常规能力，实施时验证）。
- 展会现场只有一支 Stick 与一块 T5AI 板，协议暂不做设备配对绑定；如未来多套并存，可在厂商数据中追加设备 ID 字段过滤。
