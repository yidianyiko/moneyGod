#!/usr/bin/env python3
"""GIF pipeline for cyber_fortune v2.

scene_change_gifs/*.gif -> resize to 480x320 -> quantize palette ->
optimized GIF in t5-dev/assets_build/gif/ -> C array (raw GIF bytes +
lv_img_dsc_t, same layout as examples/graphics/lvgl_gif/src/tuya_gif2.c)
in apps/cyber_fortune/src/assets/gif_<name>.c

Requires Pillow (run with core-engine/.venv/bin/python).
"""
import os
import sys

from PIL import Image, ImageSequence

HERE = os.path.dirname(os.path.abspath(__file__))
SRC_DIR = os.path.join(HERE, "../../scene_change_gifs")
BUILD_DIR = os.path.join(HERE, "../assets_build/gif")
OUT_DIR = os.path.join(HERE, "../TuyaOpen/apps/cyber_fortune/src/assets")

TARGET_W, TARGET_H = 480, 320

# (source file, C identifier, palette colors)
# 黑白像素风的用 16 色足够;彩色的(opening_1/getting_lottery)给 64 色
JOBS = [
    ("opening_1_01.gif", "gif_opening_1", 64),
    ("opening_2_01.gif", "gif_opening_2", 16),
    ("opening_3_01.gif", "gif_opening_3", 16),
    ("opening_4_01.gif", "gif_opening_4", 16),
    ("opening_5_01.gif", "gif_opening_5", 16),
    ("opening_6_01.gif", "gif_opening_6", 16),
    ("thinking.gif", "gif_thinking", 16),
    ("getting_lottery.gif", "gif_getting_lottery", 64),
    ("lottery_get.gif", "gif_lottery_get", 16),
]


def process_gif(src_path: str, dst_path: str, colors: int) -> None:
    im = Image.open(src_path)
    frames = []
    durations = []
    for frame in ImageSequence.Iterator(im):
        dur = frame.info.get("duration", 100)
        rgb = frame.convert("RGB")
        if rgb.size != (TARGET_W, TARGET_H):
            rgb = rgb.resize((TARGET_W, TARGET_H), Image.LANCZOS)
        pal = rgb.quantize(colors=colors, method=Image.MEDIANCUT, dither=Image.NONE)
        # 清掉继承自源 GIF 的透明/处置信息,避免 Pillow 保存报错
        pal.info.pop("transparency", None)
        pal.info.pop("disposal", None)
        frames.append(pal)
        durations.append(dur)
    frames[0].save(
        dst_path,
        save_all=True,
        append_images=frames[1:],
        duration=durations,
        loop=0,
        optimize=True,
        disposal=1,
    )


def emit_c(gif_path: str, name: str, out_path: str) -> int:
    with open(gif_path, "rb") as f:
        data = f.read()
    guard = "LV_ATTRIBUTE_IMG_%s" % name.upper()
    lines = []
    lines.append("#ifdef __has_include")
    lines.append('    #if __has_include("lvgl.h")')
    lines.append("        #ifndef LV_LVGL_H_INCLUDE_SIMPLE")
    lines.append("            #define LV_LVGL_H_INCLUDE_SIMPLE")
    lines.append("        #endif")
    lines.append("    #endif")
    lines.append("#endif")
    lines.append("")
    lines.append("#if defined(LV_LVGL_H_INCLUDE_SIMPLE)")
    lines.append('    #include "lvgl.h"')
    lines.append("#else")
    lines.append('    #include "lvgl/lvgl.h"')
    lines.append("#endif")
    lines.append("")
    lines.append("#ifndef LV_ATTRIBUTE_MEM_ALIGN")
    lines.append("#define LV_ATTRIBUTE_MEM_ALIGN")
    lines.append("#endif")
    lines.append("")
    lines.append("#ifndef %s" % guard)
    lines.append("#define %s" % guard)
    lines.append("#endif")
    lines.append("")
    lines.append(
        "const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST %s\nuint8_t %s_map[] = {"
        % (guard, name)
    )
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        lines.append("    " + "".join("0x%02x, " % b for b in chunk))
    lines.append("};")
    lines.append("")
    lines.append("const lv_img_dsc_t %s = {" % name)
    lines.append("  .header.w = %d," % TARGET_W)
    lines.append("  .header.h = %d," % TARGET_H)
    lines.append("  .data_size = %d," % len(data))
    lines.append("  .data = %s_map," % name)
    lines.append("};")
    lines.append("")
    with open(out_path, "w") as f:
        f.write("\n".join(lines))
    return len(data)


def main() -> None:
    os.makedirs(BUILD_DIR, exist_ok=True)
    os.makedirs(OUT_DIR, exist_ok=True)
    total = 0
    for src_name, c_name, colors in JOBS:
        src = os.path.join(SRC_DIR, src_name)
        if not os.path.exists(src):
            print("MISSING: %s" % src)
            sys.exit(1)
        mid = os.path.join(BUILD_DIR, c_name + ".gif")
        out = os.path.join(OUT_DIR, c_name + ".c")
        process_gif(src, mid, colors)
        size = emit_c(mid, c_name, out)
        total += size
        print("%-24s %4d KB -> %s" % (c_name, size // 1024, os.path.basename(out)))
    print("TOTAL: %d KB" % (total // 1024))


if __name__ == "__main__":
    main()
