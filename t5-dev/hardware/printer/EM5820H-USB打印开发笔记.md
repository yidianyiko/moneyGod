# EM5820H USB 打印 — 开发笔记（2026-07 落地版）

> 赛博财神庙单板方案：T5AI-Board 直接通过板载 USB-A 口驱动 EM5820H 热敏打印机。
> 抽签出结果后自动打印签文小票。本文是全部实现细节与踩坑记录。

## 1. 硬件与打印机身份

| 项 | 值 |
|---|---|
| 打印机 | 优库 EM5820H（58mm 面板式热敏，外置 5V 供电） |
| 自报身份 | `MANUFACTURER:;COMMAND SET:TSC/ESC/POS;MODEL:58A;COMMENT:dw-printer`（IEEE1284 GET_DEVICE_ID） |
| USB | VID=0x1d81 PID=0x5721，接口 Class:0x07 Subclass:0x01 Protocol:0x02（标准 USB 打印机类，双向） |
| 指令集 | ESC/POS（实测有效）；TSC 也支持但未使用 |
| 供电 | 5V 工作 1.3~1.6A、峰值 2.1A —— 电源必须 ≥2A |
| 连接 | 打印机 MINI-USB-B ↔ 板载 USB-A 口 |

文档：`hardware/printer/`（DW 版规格书 + 用户手册 PDF + 手册文本版）。

## 2. USB Host 使能配方（T5AI-Board）

```c
/* GPIO28 = 板载 USB VBUS 开关（来源：tkl_mftest.c 工厂测试） */
TUYA_GPIO_BASE_CFG_T cfg = {.mode=TUYA_GPIO_PULLUP, .direct=TUYA_GPIO_OUTPUT, .level=TUYA_GPIO_LEVEL_HIGH};
tkl_gpio_init(TUYA_GPIO_NUM_28, &cfg);
tkl_gpio_write(TUYA_GPIO_NUM_28, TUYA_GPIO_LEVEL_HIGH);
bk_usb_open(0);   /* 0 = USB_HOST_MODE，平台默认已编译 CONFIG_USB_HOST=y */
```

## 3. 自研 CherryUSB 打印机类驱动

平台自带的 `usbh_printer.c` 不可用（未编译进 CMake、协议匹配要求 0x00、代码有 bug）。
自研驱动按 `usbh_ch34x.c` 模式实现，**仅按 Class=0x07 匹配**：

| 文件 | 作用 |
|---|---|
| `platform/T5AI/t5_os/ap/components/bk_usb/bk_usbh_printer/bk_usbh_printer.{c,h}` | 驱动本体：连接时激活 bulk in/out 管道、dump IEEE1284 身份；`bk_usbh_printer_is_connected()` / `bk_usbh_printer_write()` |
| `.../bk_usb/CherryUSB/driver/usb_driver.c` | `bk_usb_init_all_device_driver_sw()` 里调用 `bk_usbh_printer_class_register()`（Beken 是**运行时注册**类驱动表，不是链接段） |
| `.../bk_usb/CMakeLists.txt` | CONFIG_USB_HOST 下加入 inc + src |
| `.../CherryUSB/core/usbh_core.c` | 加了 bk_printf 枚举探针（Class/Protocol dump），保留便于日后排查 |
| `platform/.../tkl_mftest.c` | `tkl_mftest_usb_info()` 里加了 VID/PID 打印 |

要点：
- CherryUSB 原生日志是 BK_LOGD/BK_LOGV，默认被过滤 —— 排查必须用 `bk_printf`
- DMA 缓冲区需 `USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX`
- 写入走 `usbh_bulk_urb_fill` + `usbh_submit_urb`，按 ≤512B 分块

## 4. 应用层（apps/cyber_fortune）

| 文件 | 作用 |
|---|---|
| `src/fortune_printer.c` + `include/fortune_printer.h` | 打印工作线程（信号量队列，UI 零阻塞）+ ESC/POS 小票排版 |
| `src/fortune_gbk_map.c` | 自动生成的 Unicode→GBK 表（1169 字，二分查找），覆盖签谱全部汉字 |
| `t5-dev/tools/gen_gbk_map.py` | **签谱数据（qianpu_data.c）更新后必须重跑**，重新生成映射表 |
| `src/fortune_ui.c` | `draw_timer_cb()` 出结果时调 `fortune_printer_print_lot(lot)` |
| `src/tuya_main.c` | 启动时 `usb_host_probe_start()` + `fortune_printer_init()` |

小票版式（58mm = 32 ASCII 列 / 16 汉字列）：
居中双倍"赛博财神庙" → 第 X 签 · 等级 → 倍高签题 → 主题 → 分隔线 → 签诗 → 分隔线 → 左对齐白话解签 → 页脚 → 5×LF 走纸过撕纸位。

常用 ESC/POS：`1B 40` 初始化；`1B 61 n` 对齐；`1D 21 n` 字号（0x11=倍宽倍高）；中文直接发 GBK 双字节。

## 5. 踩坑记录（按杀伤力排序）

1. **热敏纸装反 → 走纸正常但完全空白**。热敏纸只有一面有涂层；指甲快速划纸，发黑面为热敏面，须朝向打印头。验证手段：开机长按 FEED 打自检页（不经过 USB，纯硬件自检）。
2. **CherryUSB 枚举日志不可见**：默认日志级别过滤掉 DBG/VBS，看起来像"没枚举"，实际枚举成功。加 `bk_printf` 探针确认。
3. **平台 stock usbh_printer.c 不可用**：没进 CMake、匹配条件错、`intf+1` 下标 bug。
4. **Beken 类驱动是运行时注册**：往 `usbh_class_info_table[]` 注册，不是标准 CherryUSB 的链接段方式；新驱动必须在 `bk_usb_init_all_device_driver_sw()` 挂上。
5. GET_PORT_STATUS 控制请求此机型返回 -116（不支持），GET_DEVICE_ID 正常 —— 不要依赖 port status 判断缺纸。
6. 双模打印机（TSC/ESC/POS）如疑似模式不对：ESC/POS 发 `1D 28 41 02 00 00 02`（测试页）、TSC 发 `SELFTEST\r\n`，哪个出纸就在哪个模式。本机 ESC/POS 直接可用，无需切换。

## 6. 验证记录

- 枚举 → 类驱动加载 → bulk-out 全链路日志验证通过（字节数 ACK 一致）
- 中文 GBK、居中、倍大字号实打验证通过
- 抽签联动：`[fortune-printer] lot 30 ticket: 426 bytes built, 426 sent`，实体小票输出正常

## 7. 日常迭代工作流

```bash
cd t5-dev
bash cf_build.sh     # 编译
bash cf_flash.sh     # 烧录（~61s，自动重启）
bash cf_monitor.sh   # 串口监视（460800，日志 tee 到 /tmp/cf_mon.log）
```
