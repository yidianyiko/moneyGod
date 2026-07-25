# T5AI 侧 BLE 摇签接收设计（Stick 联动 · T5 侧）

日期：2026-07-25
状态：已确认设计，**暂缓实施**——等待 cyber-fortune-v2
（`2026-07-25-cyber-fortune-v2-design.md`）实施完成后单独排期，避免并行修改
`apps/cyber_fortune` 产生冲突。

## 背景

StickS3 摇签遥控器（见 `2026-07-25-sticks3-shake-draw-design.md`）通过无连接 BLE 广播
发送摇签事件。本 spec 描述 T5AI-Board（TuyaOpen `apps/cyber_fortune`）接收广播并触发
求签流程的改动。**通信协议以 Stick 侧 spec 第 1 节为共享契约，不得单方变更。**

## 交互语义（已与用户确认）

1. **两者并存**：触屏「求签」按钮保留，摇签事件等价触发；Stick 离线不影响触屏流程。
2. **全局响应**：待机页收到事件=求签；结果页收到事件=回待机并立即求签；求签动画中忽略。

## 改动内容

> 注意：以下基于 v2 实施前的代码结构（`fortune_ui.c` 的 `s_drawing`/`draw_btn_event_cb`），
> 实施时需对照 v2 落地后的实际结构做等价映射。

- 新增 `fortune_ble_remote.c/h`（约 150 行）：
  - `tal_ble_bt_init(TAL_BLE_ROLE_CENTRAL, cb)` + 常驻扫描（参考 `examples/ble/ble_central`）。
  - 扫描回调（非 LVGL 线程）：过滤厂商数据魔数 "CF" → 校验协议版本/事件类型 → 事件序号
    去重（与上次不同才触发）→ 置 `volatile` 触发标志。
- **线程安全**：UI 模块增加 **100ms LVGL 轮询 timer** 消费标志位；所有 UI 变更保持在
  LVGL 线程内，零跨线程调用。
- UI 模块新增公开函数 `fortune_ui_trigger_draw()`（全局响应语义）：
  - 待机页 → 等价点击求签按钮；
  - 结果页 → 回待机并立即开始求签；
  - 求签进行中（`s_drawing` 或 v2 中的等价状态）→ 忽略。
- `tuya_main.c`：UI 初始化完成后调用 `fortune_ble_remote_init()`；BLE 初始化失败仅打
  日志降级，触屏流程不受影响。
- `app_default.config`：启用蓝牙相关配置项（具体 Kconfig 符号实施时确认）。

## 容错

| 场景 | 行为 |
|---|---|
| Stick 没电/丢失/离场 | 触屏求签照常可用 |
| 广播包重复接收 | 事件序号去重，只触发一次 |
| 求签进行中收到事件 | 忽略 |
| BLE 初始化失败 | 打日志降级，主流程不受影响 |

## 测试计划

1. **单侧**：monitor 日志确认扫描、魔数过滤、序号去重、非 LVGL 线程零 UI 调用。
2. **端到端**：摇 Stick → 大屏出签 → 打印机出票；结果页摇动 → 重新求签。
3. **鲁棒性**：连续事件仅按冷却节奏出签；Stick 拔电重启后联动无需干预恢复。

## 假设

- T5AI 的 BLE 扫描与现有 WiFi/后端 HTTPS 请求共存无冲突（TuyaOpen 常规能力，实施时验证）。
- v2 实施可能重构 UI 状态机；本 spec 的触发语义不变，函数挂载点按 v2 实际结构调整。
