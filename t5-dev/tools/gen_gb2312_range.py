#!/usr/bin/env python3
"""Emit charset files for lv_font_conv --symbols.

- gb2312_l1.txt : GB2312 level-1 hanzi (0xB0A1-0xD7F9, 3755 chars)
                  + common full-width punctuation from the symbol area
- title36.txt   : narrow charset for the 36px title font
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "../fontbuild")

# GB2312 一级汉字区: 0xB0A1 ~ 0xD7F9
chars = []
for hi in range(0xB0, 0xD8):
    for lo in range(0xA1, 0xFF):
        try:
            chars.append(bytes([hi, lo]).decode("gb2312"))
        except UnicodeDecodeError:
            continue

PUNCT = "，。！？：；、“”‘’（）《》〈〉【】…—～·、％℃"
TITLE = "赛博财神庙第签求解诗上中下大吉运势事业姻缘学健康今日打印文再一支0123456789·随心转诚则灵"

os.makedirs(OUT_DIR, exist_ok=True)
with open(os.path.join(OUT_DIR, "gb2312_l1.txt"), "w", encoding="utf-8") as f:
    f.write("".join(chars) + PUNCT)
with open(os.path.join(OUT_DIR, "title36.txt"), "w", encoding="utf-8") as f:
    f.write("".join(sorted(set(TITLE))))

print("level-1 hanzi: %d, punct: %d, title36: %d"
      % (len(chars), len(PUNCT), len(set(TITLE))))
