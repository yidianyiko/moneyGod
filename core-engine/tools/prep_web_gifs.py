#!/usr/bin/env python3
"""Web GIF pipeline —— 把 scene_change_gifs/ 的原始动画压成网页可用的静态资源。

scene_change_gifs/*.gif -> 等比缩放 -> 调色板量化 -> optimize
-> core-engine/src/a2a_server/web/gifs/<name>.gif

和 t5-dev/tools/prep_gifs.py 用的是同一批源素材,但目标不同:那边出 480x320 的
C 数组喂 LVGL,这边出网页直接 <img src="/gifs/xxx.gif"> 用的文件。源素材里
opening_1(1536x1024)/getting_lottery(1016x677) 对网页太大,统一压到 480 宽以内。

产物已提交进仓库,只有换素材时才需要重跑(需要 Pillow,core-engine/.venv 里没装,
用系统 python3 或先 pip install pillow):

    python3 core-engine/tools/prep_web_gifs.py
"""
from __future__ import annotations

import os
import sys

from PIL import Image, ImageSequence

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "../.."))
SRC_DIR = os.path.join(REPO, "scene_change_gifs")
OUT_DIR = os.path.join(REPO, "core-engine/src/a2a_server/web/gifs")

# (源文件, 输出名, 目标宽度, 调色板色数)
# 黑白像素风 16 色足够;opening_1 / getting_lottery 是彩色的,给 64 色。
JOBS = [
    ("opening_1_01.gif", "hero.gif", 480, 64),
    ("opening_2_01.gif", "opening_2.gif", 320, 16),
    ("opening_3_01.gif", "opening_3.gif", 320, 16),
    ("opening_4_01.gif", "opening_4.gif", 320, 16),
    ("opening_5_01.gif", "opening_5.gif", 320, 16),
    ("opening_6_01.gif", "opening_6.gif", 320, 16),
    ("thinking.gif", "thinking.gif", 400, 16),
    ("getting_lottery.gif", "shaking.gif", 400, 64),
    ("lottery_get.gif", "lottery_get.gif", 400, 16),
]


def process(src_path: str, dst_path: str, width: int, colors: int) -> int:
    im = Image.open(src_path)
    frames, durations = [], []
    for frame in ImageSequence.Iterator(im):
        durations.append(frame.info.get("duration", 100))
        rgb = frame.convert("RGB")
        if rgb.width != width:
            height = round(rgb.height * width / rgb.width)
            rgb = rgb.resize((width, height), Image.LANCZOS)
        pal = rgb.quantize(colors=colors, method=Image.MEDIANCUT, dither=Image.NONE)
        # 清掉继承自源 GIF 的透明/处置信息,避免 Pillow 保存报错
        pal.info.pop("transparency", None)
        pal.info.pop("disposal", None)
        frames.append(pal)
    frames[0].save(
        dst_path,
        save_all=True,
        append_images=frames[1:],
        duration=durations,
        loop=0,
        optimize=True,
        disposal=1,
    )
    return os.path.getsize(dst_path)


def main() -> None:
    os.makedirs(OUT_DIR, exist_ok=True)
    total = 0
    for src_name, out_name, width, colors in JOBS:
        src = os.path.join(SRC_DIR, src_name)
        if not os.path.exists(src):
            print("MISSING: %s" % src)
            sys.exit(1)
        dst = os.path.join(OUT_DIR, out_name)
        size = process(src, dst, width, colors)
        total += size
        print("%-18s %6.1f KB  (%s)" % (out_name, size / 1024, src_name))
    print("TOTAL: %.1f KB" % (total / 1024))


if __name__ == "__main__":
    main()
