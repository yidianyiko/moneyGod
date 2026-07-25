"""赛博财神庙求签业务:服务端加权抽档 + LLM 生成签文 + 兜底签。

供 /api/fortune/draw 路由调用。签文面向 T5AI 板端展示与热敏打印,
必须只使用 GB2312 一级字库内的常用汉字。
"""
from __future__ import annotations

import logging
import random

from .config import SEED_MODEL
from .llm import ask_json

logger = logging.getLogger(__name__)

# (档位名, 分值 1-5, 概率权重) —— 与 spec 约定一致
GRADES: list[tuple[str, int, float]] = [
    ("上上", 5, 0.15),
    ("上", 4, 0.30),
    ("中", 3, 0.35),
    ("下", 2, 0.15),
    ("下下", 1, 0.05),
]

CATEGORIES = ["财运", "事业", "姻缘", "学业", "健康", "今日运势"]

# LLM 单次预算 12 秒。关闭思考链后实测约 6 秒,留余量应对偶发抖动;
# 与板端 HTTP 超时(10s)/思考动画上限(12s)对齐。
LLM_TIMEOUT_SECONDS = 12.0

_SYSTEM = (
    "你是赛博财神庙的电子财神,语气俏皮但不轻浮,像一个看透世事又爱开玩笑的老朋友。"
    "你写的签文全部用现代白话,禁止成语典故套话,禁止文言腔。"
    "只能使用最常用的简体汉字(GB2312 一级字库),禁止任何生僻字、异体字、繁体字。"
)


def draw_grade() -> tuple[str, int]:
    """服务端加权随机抽档位,返回 (档位名, 分值)。"""
    r = random.random()
    acc = 0.0
    for name, score, weight in GRADES:
        acc += weight
        if r < acc:
            return name, score
    return GRADES[-1][0], GRADES[-1][1]


def _build_prompt(category: str, question: str, answer: str, grade: str) -> str:
    return (
        f"用户来赛博财神庙求签,类目是「{category}」。\n"
        f"庙里问了用户:「{question}」,用户的回答是:「{answer}」。\n"
        f"这次抽到的签的档位已经定了:「{grade}」。\n\n"
        "请围绕用户的处境写一支签,输出 JSON,字段:\n"
        '- "poem": 数组,四句白话签诗,每句 7~10 个字,朗朗上口,贴合档位气质\n'
        '- "explanation": 解签,60~90 字,必须针对用户的问题和回答,给出具体的看法\n'
        '- "advice": 行动建议,30~50 字,具体可执行,不要空话\n\n'
        "要求:全部现代白话;不要成语典故;不要文言;只用常用简体汉字;"
        "档位是「下」或「下下」时也要给人台阶和盼头,不吓人。"
    )


# 按档位的通用兜底文案(LLM 失败时按 grade 取用,适配所有类目)
_FALLBACK_BY_GRADE: dict[str, dict] = {
    "上上": {
        "poem": ["好事排队到门口", "你只管抬脚往前走", "风向水向都帮你", "别忘了留一句谢谢"],
        "explanation": "这一签是顺风局。你惦记的那件事,眼下阻力最小,拖着反而浪费好时机。放心大胆去推进,遇到人帮你,记得接住。",
        "advice": "今天就把最想做的那件事往前推一步,别等明天。",
    },
    "上": {
        "poem": ["路已经铺了大半", "剩下几步靠自己", "别人夸你先别飘", "稳住节奏就是赢"],
        "explanation": "大方向没问题,细节上还差一点火候。你回答里提到的顾虑,其实是可以解决的小问题,别把它想成大山。",
        "advice": "把顾虑写下来拆成三小步,先做最容易的那步。",
    },
    "中": {
        "poem": ["不好不坏也是路", "急着翻盘容易输", "手里的事先做完", "机会喜欢干净桌"],
        "explanation": "现在是平稳期,谈不上惊喜也没有坑。与其焦虑没发生的事,不如把手头的事收个尾,变化往往在你收拾利落之后才来。",
        "advice": "本周清掉一件拖了很久的小事,给新机会腾位置。",
    },
    "下": {
        "poem": ["逆风不代表输局", "只是提醒你减速", "绕一小段冤枉路", "回头看全是伏笔"],
        "explanation": "近期确实有点不顺,但属于可以熬过去的那种。你的问题里其实已经藏着答案:先别硬碰硬,换个角度或者换个时间再试。",
        "advice": "近三天只守不攻:少做大决定,多睡觉多观察。",
    },
    "下下": {
        "poem": ["坏天气总会过去", "伞先撑好别硬淋", "旧的不去新不来", "财神陪你等天晴"],
        "explanation": "这签看着吓人,其实是让你彻底放下侥幸:该止损的止损,该道歉的道歉,该休息的休息。把底守住,翻盘只是时间问题。",
        "advice": "立刻停掉最耗你的那件事,先保存好体力和本金。",
    },
}

# {category: {grade: entry}} —— 结构与板端约定一致,便于测试校验
FALLBACK: dict[str, dict[str, dict]] = {
    cat: {grade: dict(entry) for grade, entry in _FALLBACK_BY_GRADE.items()}
    for cat in CATEGORIES
}


def _fallback_result(category: str, grade: str, score: int) -> dict:
    cat = category if category in FALLBACK else "今日运势"
    entry = FALLBACK[cat].get(grade, FALLBACK[cat]["中"])
    return {
        "lot_no": random.randint(1, 100),
        "grade": grade,
        "grade_score": score,
        "poem": list(entry["poem"]),
        "explanation": entry["explanation"],
        "advice": entry["advice"],
    }


def _sanitize_text(text: str) -> str:
    """剔除 GBK 编不出来的字符,避免板端字体/打印缺字。"""
    out = []
    for ch in text:
        try:
            ch.encode("gbk")
            out.append(ch)
        except UnicodeEncodeError:
            continue
    return "".join(out)


def generate_fortune(category: str, question: str, answer: str) -> dict:
    """抽档 + LLM 生成签文;LLM 失败时返回服务端兜底签,保证 200。"""
    grade, score = draw_grade()
    try:
        raw = ask_json(
            _build_prompt(category, question, answer, grade),
            model=SEED_MODEL,
            system=_SYSTEM,
            retries=0,
            timeout=LLM_TIMEOUT_SECONDS,
            thinking="disabled",  # 签文为创意类任务,关闭思考链 30s->6s
        )
        if not isinstance(raw, dict):
            raise ValueError(f"LLM 返回类型异常: {type(raw)}")
        poem = raw.get("poem")
        explanation = raw.get("explanation")
        advice = raw.get("advice")
        if (
            not isinstance(poem, list)
            or len(poem) != 4
            or not all(isinstance(p, str) and p.strip() for p in poem)
            or not isinstance(explanation, str)
            or not explanation.strip()
            or not isinstance(advice, str)
            or not advice.strip()
        ):
            raise ValueError(f"LLM 返回字段不完整: {raw!r}")
        return {
            "lot_no": random.randint(1, 100),
            "grade": grade,
            "grade_score": score,
            "poem": [_sanitize_text(p.strip()) for p in poem],
            "explanation": _sanitize_text(explanation.strip()),
            "advice": _sanitize_text(advice.strip()),
        }
    except Exception as err:  # noqa: BLE001 - 任何失败都走兜底
        logger.warning("fortune LLM 失败,走兜底签: %s", err)
        return _fallback_result(category, grade, score)


__all__ = ["GRADES", "CATEGORIES", "FALLBACK", "draw_grade", "generate_fortune"]
