# StickS3 摇签遥控器固件实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** StickS3 固件——摇动检测 → BLE 广播摇签事件 + 本地 GIF 动画/音效反馈。

**Architecture:** PlatformIO + Arduino + M5Unified。纯逻辑（摇动检测、广播载荷）与硬件绑定层分离，纯逻辑在 native 环境跑单测；硬件行为靠真机冒烟测试。主循环是简单两态状态机：IDLE（循环播待机 GIF）→ TRIGGERED（音效 + 摇签 GIF + BLE 广播连发）→ 回 IDLE。

**Tech Stack:** M5Unified（IMU/屏幕/扬声器/电源）、arduino-esp32 内置 BLE（Bluedroid）、bitbank2/AnimatedGIF（GIF 解码）、Unity（native 单测）。

**Spec:** `docs/superpowers/specs/2026-07-25-sticks3-shake-draw-design.md`（协议第 1 节为共享契约）

**本地资料:** `stick-dev/hardware/`（原理图/数据手册）、`stick-dev/docs/`（Arduino 教程 PDF）

---

## 文件结构

```
stick-dev/
├── cf_stick/                      # PlatformIO 工程
│   ├── platformio.ini
│   ├── src/
│   │   ├── main.cpp               # setup/loop + 状态机
│   │   ├── config.h               # 所有可调常量（阈值/冷却/音量/协议字段）
│   │   ├── shake_detector.h       # 纯逻辑：摇动检测（可单测）
│   │   ├── ble_beacon.h/.cpp      # 载荷构造（纯逻辑）+ 广播启停（硬件）
│   │   ├── ui_anim.h/.cpp         # GIF 播放 + 电量角标
│   │   └── assets/
│   │       ├── gif_idle.h         # thinking 240x135 C 数组
│   │       ├── gif_shake.h        # getting_lottery 240x135 C 数组
│   │       └── wav_shake.h        # 摇签音效 C 数组
│   ├── test/
│   │   └── test_native/
│   │       └── test_logic.cpp     # 摇动检测 + 载荷单测
│   └── tools/
│       ├── make_assets.py         # GIF 缩放转 C 数组 + WAV 生成
│       └── (输出写入 src/assets/)
├── cf_stick_build.sh              # 编译
├── cf_stick_flash.sh              # 烧录（VID 0x303A 自动选口）
└── cf_stick_monitor.sh            # 串口监视
```

---

### Task 1: PlatformIO 工程脚手架 + 最小固件

**Files:**
- Create: `stick-dev/cf_stick/platformio.ini`
- Create: `stick-dev/cf_stick/src/main.cpp`
- Create: `stick-dev/cf_stick/src/config.h`

- [ ] **Step 1: 确认 pio 可用**

Run: `pio --version || pip3 install platformio`
Expected: `PlatformIO Core, version 6.x`

- [ ] **Step 2: 写 platformio.ini**（官方推荐配置 + native 测试环境）

```ini
[env:m5stack-sticks3]
platform = espressif32@6.12.0
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.partitions = default_8MB.csv
board_build.arduino.memory_type = qio_opi
build_flags =
    -DESP32S3
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
    -DCORE_DEBUG_LEVEL=3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
lib_deps =
    m5stack/M5Unified@^0.2.7
    bitbank2/AnimatedGIF@^2.1.1
monitor_speed = 115200
test_ignore = test_native

[env:native]
platform = native
build_flags = -std=c++14
test_filter = test_native
```

- [ ] **Step 3: 写 config.h**（所有调参常量集中在此）

```cpp
#pragma once
/* ---- 摇动检测 ---- */
#define SHAKE_PEAK_G        2.0f    /* 加速度模长峰值阈值 (g) */
#define SHAKE_PEAK_COUNT    3       /* 窗口内需要的峰个数 */
#define SHAKE_WINDOW_MS     800     /* 峰计数窗口 */
#define SHAKE_COOLDOWN_MS   2000    /* 触发后冷却 */
#define IMU_POLL_MS         20      /* 50Hz 采样 */
/* ---- BLE 广播 ---- */
#define BLE_BURST_MS        1500    /* 连发时长 */
#define BLE_ADV_INTERVAL    0x50    /* 50ms (0.625ms 单位) */
/* ---- 音频 ---- */
#define SPK_VOL_USB         200     /* USB 供电音量 (0-255) */
#define SPK_VOL_BATT        180     /* 电池供电音量，<75% 上限防重启 */
```

- [ ] **Step 4: 写最小 main.cpp**（M5 初始化 + 屏幕横屏点亮 + 心跳日志）

```cpp
#include <M5Unified.h>
#include "config.h"

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);           /* 横屏 240x135 */
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_GOLD);
    M5.Display.setTextSize(2);
    M5.Display.drawString("CF Stick OK", 10, 10);
    Serial.begin(115200);
}

void loop() {
    M5.update();
    static uint32_t last = 0;
    if (millis() - last > 1000) {
        last = millis();
        Serial.printf("[hb] up=%lus batt=%d%%\n", millis() / 1000, M5.Power.getBatteryLevel());
    }
    delay(10);
}
```

- [ ] **Step 5: 编译验证**

Run: `cd stick-dev/cf_stick && pio run -e m5stack-sticks3`
Expected: `SUCCESS`，首次会下载 espressif32 平台（中国网络下若超时，unset 代理重试）

- [ ] **Step 6: Commit**

```bash
git add stick-dev/cf_stick
git commit -m "feat(stick): PlatformIO 工程脚手架 + 最小固件"
```

---

### Task 2: 构建/烧录/监视脚本

**Files:**
- Create: `stick-dev/cf_stick_build.sh`
- Create: `stick-dev/cf_stick_flash.sh`
- Create: `stick-dev/cf_stick_monitor.sh`

- [ ] **Step 1: 写三个脚本**

`cf_stick_build.sh`:
```bash
#!/bin/bash
set -e
cd "$(dirname "$0")/cf_stick"
unset http_proxy https_proxy all_proxy
pio run -e m5stack-sticks3 "$@"
```

`cf_stick_flash.sh`（用 VID 0x303A=Espressif 自动找口，不靠 usbmodem 前缀——T5 的 CH342 也是 usbmodem）:
```bash
#!/bin/bash
set -e
cd "$(dirname "$0")/cf_stick"
unset http_proxy https_proxy all_proxy
# Espressif VID 0x303A → ioreg 十进制 12346；取其 usbmodem 设备
PORT=$(pio device list --json-output | python3 -c "
import json,sys
for d in json.load(sys.stdin):
    hwid = d.get('hwid','')
    if '303A' in hwid.upper():
        print(d['port']); break
")
if [ -z "$PORT" ]; then echo "未找到 StickS3 (VID 303A)"; exit 1; fi
echo "烧录到 $PORT"
pio run -e m5stack-sticks3 -t upload --upload-port "$PORT"
```

`cf_stick_monitor.sh`:
```bash
#!/bin/bash
cd "$(dirname "$0")/cf_stick"
PORT=$(pio device list --json-output | python3 -c "
import json,sys
for d in json.load(sys.stdin):
    if '303A' in d.get('hwid','').upper():
        print(d['port']); break
")
pio device monitor -b 115200 ${PORT:+-p "$PORT"}
```

- [ ] **Step 2: 加执行权限并冒烟**

Run: `chmod +x stick-dev/cf_stick_*.sh && ./stick-dev/cf_stick_build.sh`
Expected: `SUCCESS`

- [ ] **Step 3: 真机烧录冒烟**

Run: `./stick-dev/cf_stick_flash.sh && ./stick-dev/cf_stick_monitor.sh`
Expected: 屏幕显示 "CF Stick OK"，串口每秒打 `[hb]` 心跳与电量

- [ ] **Step 4: Commit**

```bash
git add stick-dev/cf_stick_build.sh stick-dev/cf_stick_flash.sh stick-dev/cf_stick_monitor.sh
git commit -m "feat(stick): 构建/烧录/监视脚本（VID 303A 自动选口）"
```

---

### Task 3: 摇动检测纯逻辑（TDD）

**Files:**
- Create: `stick-dev/cf_stick/src/shake_detector.h`（header-only，双环境可编译）
- Create: `stick-dev/cf_stick/test/test_native/test_logic.cpp`

- [ ] **Step 1: 写失败的单测**

```cpp
#include <unity.h>
#include "shake_detector.h"

/* 模拟 50Hz 采样：静止 → 3 次猛烈晃动 → 应触发一次，冷却期内再晃不触发 */
void test_shake_triggers_once(void) {
    ShakeDetector det;
    uint32_t ms = 0;
    bool fired = false;
    /* 静止 1g */
    for (int i = 0; i < 50; i++) { fired |= det.update(0, 0, 1.0f, ms); ms += 20; }
    TEST_ASSERT_FALSE(fired);
    /* 3 个 2.5g 峰，间隔 200ms（窗口 800ms 内） */
    for (int p = 0; p < 3; p++) {
        fired |= det.update(2.5f, 0, 0, ms); ms += 20;
        for (int i = 0; i < 9; i++) { fired |= det.update(0, 0, 1.0f, ms); ms += 20; }
    }
    TEST_ASSERT_TRUE(fired);
    /* 冷却期内继续晃：不触发 */
    bool again = false;
    for (int p = 0; p < 3; p++) {
        again |= det.update(2.5f, 0, 0, ms); ms += 20;
        for (int i = 0; i < 9; i++) { again |= det.update(0, 0, 1.0f, ms); ms += 20; }
    }
    TEST_ASSERT_FALSE(again);
}

void test_slow_peaks_no_trigger(void) {
    ShakeDetector det;
    uint32_t ms = 0;
    bool fired = false;
    /* 3 个峰但间隔 1s > 800ms 窗口：不触发 */
    for (int p = 0; p < 3; p++) {
        fired |= det.update(2.5f, 0, 0, ms); ms += 20;
        for (int i = 0; i < 49; i++) { fired |= det.update(0, 0, 1.0f, ms); ms += 20; }
    }
    TEST_ASSERT_FALSE(fired);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_shake_triggers_once);
    RUN_TEST(test_slow_peaks_no_trigger);
    return UNITY_END();
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cd stick-dev/cf_stick && pio test -e native`
Expected: FAIL（shake_detector.h 不存在）

- [ ] **Step 3: 实现 shake_detector.h**

```cpp
#pragma once
#include <stdint.h>
#include <math.h>
#include "config.h"

/* 峰值+窗口计数式摇动检测。峰 = 模长超阈值的上升沿（带回落解锁）。 */
class ShakeDetector {
public:
    /* ax/ay/az 单位 g；now_ms 单调毫秒。返回 true = 触发一次摇签 */
    bool update(float ax, float ay, float az, uint32_t now_ms) {
        if (now_ms - _last_fire < SHAKE_COOLDOWN_MS && _last_fire != 0) return false;
        float mag = sqrtf(ax * ax + ay * ay + az * az);
        bool above = mag > SHAKE_PEAK_G;
        if (above && !_armed) {
            _armed = true;
            /* 窗口过期则重新开窗 */
            if (_count == 0 || now_ms - _window_start > SHAKE_WINDOW_MS) {
                _window_start = now_ms;
                _count = 0;
            }
            _count++;
            if (_count >= SHAKE_PEAK_COUNT && now_ms - _window_start <= SHAKE_WINDOW_MS) {
                _count = 0;
                _last_fire = now_ms ? now_ms : 1;
                return true;
            }
        } else if (!above && mag < SHAKE_PEAK_G * 0.7f) {
            _armed = false;   /* 回落解锁，避免持续超阈值重复计峰 */
        }
        return false;
    }
private:
    bool _armed = false;
    uint8_t _count = 0;
    uint32_t _window_start = 0;
    uint32_t _last_fire = 0;
};
```

- [ ] **Step 4: 跑测试确认通过**

Run: `pio test -e native`
Expected: `2 Tests 0 Failures`

- [ ] **Step 5: Commit**

```bash
git add stick-dev/cf_stick/src/shake_detector.h stick-dev/cf_stick/test
git commit -m "feat(stick): 摇动检测纯逻辑 + native 单测"
```

---

### Task 4: BLE 广播载荷（TDD）+ 广播硬件层

**Files:**
- Create: `stick-dev/cf_stick/src/ble_beacon.h`
- Create: `stick-dev/cf_stick/src/ble_beacon.cpp`
- Modify: `stick-dev/cf_stick/test/test_native/test_logic.cpp`

- [ ] **Step 1: 单测追加载荷用例（协议契约，spec 第 1 节）**

test_logic.cpp 追加（include 与 RUN_TEST 同步加）:
```cpp
#include "ble_beacon.h"

void test_beacon_payload(void) {
    uint8_t buf[8];
    beacon_build_payload(buf, /*event=*/0x01, /*seq=*/42, /*batt=*/88);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[0]);  /* 厂商ID lo */
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[1]);  /* 厂商ID hi */
    TEST_ASSERT_EQUAL_HEX8(0x43, buf[2]);  /* 'C' */
    TEST_ASSERT_EQUAL_HEX8(0x46, buf[3]);  /* 'F' */
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[4]);  /* 版本 */
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[5]);  /* 事件 */
    TEST_ASSERT_EQUAL_HEX8(42,   buf[6]);  /* 序号 */
    TEST_ASSERT_EQUAL_HEX8(88,   buf[7]);  /* 电量 */
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `pio test -e native` → FAIL（ble_beacon.h 不存在）

- [ ] **Step 3: 写 ble_beacon.h**（载荷构造 inline 纯函数 + 硬件接口声明）

```cpp
#pragma once
#include <stdint.h>
#include <string.h>

#define BEACON_PAYLOAD_LEN 8

/* 纯函数：按 spec 第 1 节协议构造厂商数据 */
inline void beacon_build_payload(uint8_t out[BEACON_PAYLOAD_LEN],
                                 uint8_t event, uint8_t seq, uint8_t batt) {
    out[0] = 0xFF; out[1] = 0xFF;          /* 厂商ID（测试保留值） */
    out[2] = 0x43; out[3] = 0x46;          /* 魔数 "CF" */
    out[4] = 0x01;                         /* 协议版本 */
    out[5] = event;                        /* 事件类型 */
    out[6] = seq;                          /* 事件序号 */
    out[7] = batt;                         /* 电量 % */
}

#ifdef ARDUINO
void beacon_init(void);                              /* BLEDevice 初始化 */
void beacon_burst_start(uint8_t seq, uint8_t batt);  /* 设置载荷并开播 */
void beacon_stop(void);                              /* 停止广播 */
#endif
```

- [ ] **Step 4: 跑测试确认通过**

Run: `pio test -e native` → `3 Tests 0 Failures`

- [ ] **Step 5: 写 ble_beacon.cpp**（仅 Arduino 环境编译）

```cpp
#include "ble_beacon.h"
#ifdef ARDUINO
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include "config.h"

static BLEAdvertising *s_adv = nullptr;

void beacon_init(void) {
    BLEDevice::init("CF-Stick");
    s_adv = BLEDevice::getAdvertising();
    s_adv->setMinInterval(BLE_ADV_INTERVAL);
    s_adv->setMaxInterval(BLE_ADV_INTERVAL);
    /* 不可连接广播：纯 beacon */
    s_adv->setAdvertisementType(ADV_TYPE_NONCONN_IND);
}

void beacon_burst_start(uint8_t seq, uint8_t batt) {
    uint8_t payload[BEACON_PAYLOAD_LEN];
    beacon_build_payload(payload, 0x01, seq, batt);
    BLEAdvertisementData data;
    data.setManufacturerData(std::string((char *)payload, BEACON_PAYLOAD_LEN));
    s_adv->stop();
    s_adv->setAdvertisementData(data);
    s_adv->start();
}

void beacon_stop(void) {
    if (s_adv) s_adv->stop();
}
#endif
```

- [ ] **Step 6: 全量编译两个环境**

Run: `pio run -e m5stack-sticks3 && pio test -e native`
Expected: 均 SUCCESS/PASS

- [ ] **Step 7: Commit**

```bash
git add stick-dev/cf_stick/src/ble_beacon.* stick-dev/cf_stick/test
git commit -m "feat(stick): BLE 广播载荷(协议契约单测) + beacon 硬件层"
```

---

### Task 5: 真机联调——摇动 → 广播（nRF Connect 验证）

**Files:**
- Modify: `stick-dev/cf_stick/src/main.cpp`

- [ ] **Step 1: main.cpp 接入 IMU 轮询 + 摇动检测 + 广播连发**

```cpp
#include <M5Unified.h>
#include "config.h"
#include "shake_detector.h"
#include "ble_beacon.h"

static ShakeDetector s_shake;
static uint8_t s_seq = 0;
static uint32_t s_burst_until = 0;

void setup() {
    auto cfg = M5.config();
    cfg.internal_imu = true;
    M5.begin(cfg);
    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_GOLD);
    M5.Display.setTextSize(2);
    M5.Display.drawString("shake me", 10, 10);
    Serial.begin(115200);
    beacon_init();
}

void loop() {
    M5.update();
    static uint32_t last_poll = 0;
    uint32_t now = millis();

    if (now - last_poll >= IMU_POLL_MS) {
        last_poll = now;
        float ax, ay, az;
        if (M5.Imu.getAccel(&ax, &ay, &az)) {
            if (s_shake.update(ax, ay, az, now)) {
                s_seq++;
                uint8_t batt = (uint8_t)M5.Power.getBatteryLevel();
                Serial.printf("[shake] fired! seq=%u batt=%u%%\n", s_seq, batt);
                beacon_burst_start(s_seq, batt);
                s_burst_until = now + BLE_BURST_MS;
            }
        }
    }
    if (s_burst_until && now > s_burst_until) {
        s_burst_until = 0;
        beacon_stop();
        Serial.println("[beacon] burst end");
    }
    delay(2);
}
```

- [ ] **Step 2: 编译烧录**

Run: `./stick-dev/cf_stick_build.sh && ./stick-dev/cf_stick_flash.sh`

- [ ] **Step 3: 真机验证摇动检测**

Run: `./stick-dev/cf_stick_monitor.sh`，用力摇 Stick
Expected: 打出 `[shake] fired! seq=1 ...`，1.5s 后 `[beacon] burst end`；连摇 10 次在 2s 冷却内只触发 1 次。**若灵敏度不合适，调 config.h 的 SHAKE_PEAK_G / SHAKE_PEAK_COUNT 并记录最终值**

- [ ] **Step 4: 手机 nRF Connect 验证广播**

手机装 nRF Connect → Scan → 摇动 Stick → 找到设备，Raw data 中厂商数据应为 `FFFF 4346 01 01 <seq> <batt>`；序号随每次摇动 +1；1.5s 后广播消失

- [ ] **Step 5: Commit**

```bash
git add stick-dev/cf_stick/src/main.cpp
git commit -m "feat(stick): 摇动→BLE广播真机联调通过（nRF Connect 验证）"
```

---

### Task 6: 素材流水线（GIF 240×135 + 音效 WAV → C 数组）

**Files:**
- Create: `stick-dev/cf_stick/tools/make_assets.py`
- Create: `stick-dev/cf_stick/src/assets/gif_idle.h`（生成）
- Create: `stick-dev/cf_stick/src/assets/gif_shake.h`（生成）
- Create: `stick-dev/cf_stick/src/assets/wav_shake.h`（生成）

- [ ] **Step 1: 写 make_assets.py**（先看 `git show de4a3bc --stat` 里 T5 侧 GIF 流水线是否可直接复用，可复用则改参数调用；否则用下面实现）

```python
#!/usr/bin/env python3
"""GIF 缩放到 240x135 转 C 数组；生成摇签音效 WAV 转 C 数组。
依赖: pip3 install pillow
用法: python3 tools/make_assets.py
"""
import io, math, struct, wave
from pathlib import Path
from PIL import Image, ImageSequence

ROOT = Path(__file__).resolve().parents[3]          # moneyGod 仓库根
SRC_GIFS = {
    "gif_idle":  ROOT / "scene_change_gifs/thinking.gif",
    "gif_shake": ROOT / "scene_change_gifs/getting_lottery.gif",
}
OUT_DIR = Path(__file__).resolve().parents[1] / "src/assets"
W, H = 240, 135
MAX_FRAMES = 30      # 降帧上限，控制体积

def to_c_array(name: str, data: bytes) -> str:
    lines = [f"/* generated by make_assets.py, {len(data)} bytes */",
             "#pragma once", "#include <stdint.h>",
             f"const uint8_t {name}[] = {{"]
    for i in range(0, len(data), 16):
        lines.append("  " + ",".join(f"0x{b:02X}" for b in data[i:i+16]) + ",")
    lines.append("};")
    lines.append(f"const uint32_t {name}_len = {len(data)};")
    return "\n".join(lines) + "\n"

def convert_gif(name: str, src: Path):
    im = Image.open(src)
    frames = [f.convert("RGB").resize((W, H), Image.LANCZOS)
              for f in ImageSequence.Iterator(im)]
    step = max(1, len(frames) // MAX_FRAMES)
    frames = frames[::step]
    dur = im.info.get("duration", 100) * step
    buf = io.BytesIO()
    frames[0].save(buf, format="GIF", save_all=True, append_images=frames[1:],
                   duration=dur, loop=0, optimize=True)
    data = buf.getvalue()
    (OUT_DIR / f"{name}.h").write_text(to_c_array(name, data))
    print(f"{name}: {len(frames)} frames, {len(data)/1024:.0f} KB")

def make_chime():
    """0.6s 双音上行 chime，16bit 22050Hz mono"""
    sr, dur = 22050, 0.6
    samples = []
    for i in range(int(sr * dur)):
        t = i / sr
        f = 880 if t < 0.25 else 1320
        env = math.exp(-4 * t)
        samples.append(int(28000 * env * math.sin(2 * math.pi * f * t)))
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(b"".join(struct.pack("<h", s) for s in samples))
    data = buf.getvalue()
    (OUT_DIR / "wav_shake.h").write_text(to_c_array("wav_shake", data))
    print(f"wav_shake: {len(data)/1024:.0f} KB")

if __name__ == "__main__":
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for name, src in SRC_GIFS.items():
        convert_gif(name, src)
    make_chime()
```

- [ ] **Step 2: 运行生成素材**

Run: `cd stick-dev/cf_stick && python3 tools/make_assets.py`
Expected: 打印三个素材大小；gif 各 ≤ 500KB（超限则调低 MAX_FRAMES 重跑）

- [ ] **Step 3: 验证生成的头文件可编译**（临时在 main.cpp include 三个头 + 打印长度，编译后撤掉，或直接进 Task 7）

Run: `pio run -e m5stack-sticks3`
Expected: SUCCESS，Flash 占用 < 70%

- [ ] **Step 4: Commit**

```bash
git add stick-dev/cf_stick/tools stick-dev/cf_stick/src/assets
git commit -m "feat(stick): 素材流水线——GIF 240x135 + chime 音效转 C 数组"
```

---

### Task 7: GIF 播放 + 电量角标（ui_anim 模块）

**Files:**
- Create: `stick-dev/cf_stick/src/ui_anim.h`
- Create: `stick-dev/cf_stick/src/ui_anim.cpp`

- [ ] **Step 1: 写 ui_anim.h**

```cpp
#pragma once
#include <stdint.h>

void ui_init(void);
void ui_play_idle(void);          /* 切到待机 GIF（循环） */
void ui_play_shake(void);         /* 切到摇签 GIF（播一遍，完毕自动回待机） */
void ui_tick(void);               /* 主循环驱动：解码下一帧 + 叠加电量角标 */
bool ui_shake_playing(void);      /* 摇签动画是否还在播 */
```

- [ ] **Step 2: 写 ui_anim.cpp**（AnimatedGIF 双实例切换；帧回调直推屏幕）

```cpp
#include "ui_anim.h"
#include <M5Unified.h>
#include <AnimatedGIF.h>
#include "assets/gif_idle.h"
#include "assets/gif_shake.h"

static AnimatedGIF s_gif;
static bool s_is_shake = false;       /* 当前播放的是摇签动画 */
static uint32_t s_next_frame_ms = 0;
static uint16_t s_line[240];

/* AnimatedGIF 行回调：把一行像素推到屏幕（240x135 全屏，无偏移） */
static void gif_draw(GIFDRAW *pDraw) {
    uint8_t *s = pDraw->pPixels;
    uint16_t *pal = (uint16_t *)pDraw->pPalette;
    for (int x = 0; x < pDraw->iWidth; x++) s_line[x] = pal[s[x]];
    M5.Display.pushImage(pDraw->iX, pDraw->iY + pDraw->y, pDraw->iWidth, 1, s_line);
}

static void open_gif(const uint8_t *data, uint32_t len) {
    s_gif.close();
    s_gif.open((uint8_t *)data, len, gif_draw);
    s_next_frame_ms = 0;
}

void ui_init(void) {
    s_gif.begin(GIF_PALETTE_RGB565_BE);
    ui_play_idle();
}

void ui_play_idle(void)  { s_is_shake = false; open_gif(gif_idle, gif_idle_len); }
void ui_play_shake(void) { s_is_shake = true;  open_gif(gif_shake, gif_shake_len); }
bool ui_shake_playing(void) { return s_is_shake; }

void ui_tick(void) {
    uint32_t now = millis();
    if (now < s_next_frame_ms) return;
    int delay_ms = 0;
    int more = s_gif.playFrame(false, &delay_ms);
    s_next_frame_ms = now + (delay_ms > 0 ? delay_ms : 50);
    if (!more) {                       /* 播完一轮 */
        if (s_is_shake) { ui_play_idle(); }   /* 摇签动画只播一遍 */
        else { s_gif.reset(); }               /* 待机循环 */
    }
    /* 电量角标（每帧重绘，覆盖在 GIF 上层） */
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_GOLD, TFT_BLACK);
    M5.Display.setCursor(206, 4);
    M5.Display.printf("%3d%%", M5.Power.getBatteryLevel());
}
```

- [ ] **Step 3: 编译**

Run: `pio run -e m5stack-sticks3` → SUCCESS

- [ ] **Step 4: Commit**

```bash
git add stick-dev/cf_stick/src/ui_anim.*
git commit -m "feat(stick): AnimatedGIF 播放 + 电量角标"
```

---

### Task 8: 音效播放 + 主状态机总装

**Files:**
- Modify: `stick-dev/cf_stick/src/main.cpp`

- [ ] **Step 1: main.cpp 总装**（完整替换）

```cpp
#include <M5Unified.h>
#include "config.h"
#include "shake_detector.h"
#include "ble_beacon.h"
#include "ui_anim.h"
#include "assets/wav_shake.h"

static ShakeDetector s_shake;
static uint8_t s_seq = 0;
static uint32_t s_burst_until = 0;

static void apply_volume(void) {
    bool usb = M5.Power.isCharging() || M5.Power.getBatteryLevel() < 0;
    M5.Speaker.setVolume(usb ? SPK_VOL_USB : SPK_VOL_BATT);
}

void setup() {
    auto cfg = M5.config();
    cfg.internal_imu = true;
    cfg.internal_spk = true;
    M5.begin(cfg);
    M5.Display.setRotation(1);
    Serial.begin(115200);
    beacon_init();
    ui_init();
    apply_volume();
    Serial.println("[cf-stick] ready");
}

void loop() {
    M5.update();
    uint32_t now = millis();

    /* IMU 50Hz 轮询 */
    static uint32_t last_poll = 0;
    if (now - last_poll >= IMU_POLL_MS) {
        last_poll = now;
        float ax, ay, az;
        if (M5.Imu.getAccel(&ax, &ay, &az)) {
            if (s_shake.update(ax, ay, az, now)) {
                s_seq++;
                uint8_t batt = (uint8_t)M5.Power.getBatteryLevel();
                Serial.printf("[shake] seq=%u batt=%u%%\n", s_seq, batt);
                beacon_burst_start(s_seq, batt);      /* 1. 广播 */
                s_burst_until = now + BLE_BURST_MS;
                apply_volume();                       /* 2. 音效 */
                M5.Speaker.playWav(wav_shake, wav_shake_len);
                ui_play_shake();                      /* 3. 动画 */
            }
        }
    }
    /* 广播定时停 */
    if (s_burst_until && now > s_burst_until) {
        s_burst_until = 0;
        beacon_stop();
    }
    ui_tick();
    delay(2);
}
```

- [ ] **Step 2: 编译烧录**

Run: `./stick-dev/cf_stick_build.sh && ./stick-dev/cf_stick_flash.sh`

- [ ] **Step 3: Commit**

```bash
git add stick-dev/cf_stick/src/main.cpp
git commit -m "feat(stick): 主状态机总装——摇动→广播+音效+动画"
```

---

### Task 9: 真机验收（spec 第 5 节测试计划）

- [ ] **Step 1: 摇动检测**：monitor 下摇动灵敏度合手感；冷却内连摇 10 次仅 1 次触发。不合适则调 config.h 重新烧录，记录最终参数
- [ ] **Step 2: 广播协议**：nRF Connect 核对 `FFFF 4346 01 01 seq batt`，seq 递增，1.5s 停播
- [ ] **Step 3: 本地反馈**：待机 thinking GIF 循环流畅（无明显卡顿）；摇动 → chime 音效 + getting_lottery 播一遍 → 自动回待机；电量角标正确
- [ ] **Step 4: 供电**：拔 USB 用电池——音效播放不重启；拔电重启后一切自动恢复
- [ ] **Step 5: 修复过程中发现的问题并逐项打勾，全部通过后 commit**

```bash
git add -A stick-dev
git commit -m "test(stick): 真机验收通过，固化调参值"
```

---

### Task 10: 开发笔记沉淀

**Files:**
- Create: `stick-dev/hardware/StickS3-摇签遥控器开发笔记.md`

- [ ] **Step 1: 写笔记**，内容涵盖：硬件身份（VID 0x303A、串口识别方式）、刷机方式、BLE 协议表（与 spec 同步）、摇动检测最终调参值、素材流水线用法、验收结论、已知坑
- [ ] **Step 2: Commit**

```bash
git add stick-dev/hardware/StickS3-摇签遥控器开发笔记.md
git commit -m "docs(stick): 开发笔记沉淀"
```

---

## Self-Review 结论

- **Spec 覆盖**：协议§1→Task 4；固件§2 摇动检测→Task 3/5、GIF+音效→Task 6/7/8、供电音量→Task 8/9、脚本→Task 2；测试§5→Task 5/9 ✓
- **占位符**：无 TBD/TODO；所有代码步骤含完整代码 ✓
- **类型一致性**：`beacon_build_payload/beacon_burst_start/ui_play_shake/ui_tick` 等签名在各 Task 间一致 ✓
- **已知实施风险**（执行时验证）：① arduino-esp32 Bluedroid BLE 与 M5Unified 并存的内存占用；若紧张改 NimBLE-Arduino（API 相近）。② AnimatedGIF 回调签名随版本略有差异，以 2.1.x 头文件为准。③ M5.Imu.getAccel 返回单位即 g（M5Unified 约定）。
