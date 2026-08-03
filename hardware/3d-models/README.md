# 赛博财神庙 — 外壳 3D 模型（最终确定版）

整台设备的外壳打印件：庙顶、庙身、底座、以及固定屏幕 / 热敏打印机 / 磁悬浮环的支架。
共 13 个零件，全部是 **binary STL**，单位 **毫米**，直接丢进切片软件即可。

## 下载

**一键打包下载（推荐，3.8 MB）**

<https://github.com/yidianyiko/moneyGod/raw/master/hardware/3d-models/moneygod-shrine-stl.zip>

**单个零件** — 在 GitHub 上点开任意 `.stl` 会直接渲染 3D 预览，右上角 `Download` 即可下载：

<https://github.com/yidianyiko/moneyGod/tree/master/hardware/3d-models>

命令行：

```bash
# 只要模型，不想 clone 整个仓库（仓库里带了 TuyaOpen SDK，很大）
curl -LO https://github.com/yidianyiko/moneyGod/raw/master/hardware/3d-models/moneygod-shrine-stl.zip
unzip moneygod-shrine-stl.zip -d moneygod-stl
```

## 零件清单

尺寸为模型包围盒 `X × Y × Z`（mm），已按摆放姿态给出。

| 文件 | 零件 | 尺寸 (mm) | 三角面 |
|---|---|---|---|
| `01_roof.stl` | 庙顶（带瓦楞造型，面数最高） | 180 × 140 × 35 | 151,264 |
| `02_roof_liner.stl` | 庙顶内衬 | 164 × 124 × 6.2 | 2,156 |
| `0301.stl` | 03 组件 · 薄底板（与内衬同 XY） | 164 × 124 × 3.0 | 208 |
| `0302.stl` | 03 组件 · 外檐板 | 216 × 150 × 6.0 | 1,692 |
| `0303.stl` | 03 组件 · 庙身主体（最高件） | 154 × 120 × 105 | 35,394 |
| `04.stl` | 04 层板 | 168.1 × 128.4 × 5.9 | 2,702 |
| `08_top_platform.stl` | 顶部平台 | 174 × 134 × 4.9 | 49,320 |
| `09_maglev_ring.stl` | 磁悬浮环 | 108 × 108 × 5.0 | 768 |
| `11_base_shell.stl` | 底座外壳 | 180 × 140 × 75 | 5,128 |
| `13_display_bracket.stl` | 屏幕支架（立式薄片） | 101 × 3 × 70 | 208 |
| `16_printer_bracket.stl` | 热敏打印机支架（EM5820H） | 109 × 54.5 × 1.2 | 32 |
| `18_rear_cover.stl` | 后盖（立式薄片） | 166 × 3 × 61 | 332 |
| `19_bottom_plate.stl` | 底板 | 174 × 134 × 3.0 | 96 |

关于命名，两点需要知道：

- 编号有跳号（缺 05–07、10、12、14、15、17）。这些是设计过程中的内部编号，最终装配不需要，不是漏传。
- `0301/0302/0303/04` 出自 Shapr3D，其余出自另一套建模流程（STL 头为空）。名字保持发来时的原样，没有重命名，避免和硬件同学的图纸对不上。上表的"零件"一列是按尺寸和装配关系推断的，**装配前请以实际图纸为准**。

## 打印建议

- **最大底面 180 × 140 mm**（`01_roof` / `11_base_shell`），220 mm 及以上热床都放得下（Ender-3、Bambu A1/P1S 均可）。
- **最高件 105 mm**（`0303`）。
- `01_roof` 有 15 万面，切片会慢一点，属正常。
- `13_display_bracket`、`18_rear_cover`、`16_printer_bracket` 是 1.2–3 mm 的薄片，**平铺打印**，别立着打。
- 层高 0.2 mm、PLA、支撑视庙顶挑檐角度而定即可，没有特殊工艺要求。

## 装配相关的其它资料

- 屏幕 / 摄像头模块尺寸与原理图：[`t5-dev/hardware/`](../../t5-dev/hardware/)
- 热敏打印机 EM5820H 规格书：[`t5-dev/hardware/printer/`](../../t5-dev/hardware/printer/)

## 更新模型时

`moneygod-shrine-stl.zip` 是由同目录的 STL 打包出来的，改了 STL 记得重新生成，否则打包版会过期：

```bash
cd hardware/3d-models
python3 -c "
import zipfile, glob
with zipfile.ZipFile('moneygod-shrine-stl.zip','w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for f in sorted(glob.glob('*.stl')): z.write(f)
"
```
