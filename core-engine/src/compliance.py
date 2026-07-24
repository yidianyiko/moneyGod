"""合规护栏 —— 贯穿所有对外输出。

原则:数字由代码算,LLM 只解释;严禁收益承诺类措辞;每条对外结果必带风险提示。
本模块是纯确定性函数,不调用 LLM。
"""
from __future__ import annotations

import re

# 禁用措辞(命中即替换为中性表达,避免违规承诺)
_FORBIDDEN = [
    "稳赚", "必涨", "必跌", "保本", "包赚", "稳赢", "躺赚",
    "翻倍", "一定涨", "一定赚", "零风险", "无风险", "保证收益",
    "包赢", "只涨不跌", "稳赚不赔",
]

RISK_DISCLAIMER = (
    "【风险提示】以上为基于历史行情数据的量化分析,非投资建议,不构成任何收益承诺。"
    "市场有风险,投资需谨慎;历史表现不代表未来收益。请结合自身风险承受能力独立决策。"
    "本服务由 AI 生成,不代客理财,数据可能存在延迟或缺失。"
)


def scrub(text: str) -> str:
    """替换禁用措辞为中性表达。"""
    out = text
    for word in _FORBIDDEN:
        out = out.replace(word, "〔已按合规要求改写〕")
    return out


def has_forbidden(text: str) -> list[str]:
    """返回文本中命中的禁用措辞列表(用于自检/测试)。"""
    return [w for w in _FORBIDDEN if w in text]


def ensure_disclaimer(text: str) -> str:
    """确保输出结尾带风险提示;先做措辞清洗。

    注意:判断是否已带免责声明时,必须匹配完整的 RISK_DISCLAIMER 文案,
    不能只匹配"风险提示"这几个字——LLM 生成的报告几乎总会自带一个
    "风险提示"小节,若用它做短路判断,合规要求的免责声明(含"非玄学/
    AI 边界/不代客理财"等要素)会被永远跳过。
    """
    cleaned = scrub(text).rstrip()
    if RISK_DISCLAIMER in cleaned:
        return cleaned
    return f"{cleaned}\n\n{RISK_DISCLAIMER}"


__all__ = ["scrub", "has_forbidden", "ensure_disclaimer", "RISK_DISCLAIMER"]
