"""Strategy Agent —— 生成建议与风险判断。

结合用户画像和研究结论,产出可执行信号(buy/hold/reduce/watch)。
输出仍是"结论层",不做用户表达(表达由 M4 负责)。
"""
from __future__ import annotations

import json

from ..llm import DEEPSEEK_PRO_MODEL, ask_json
from ..schemas import Holding, Signal, UserProfile

SYSTEM = (
    "你是稳健的策略师(Strategy)。结合用户风险等级给出克制的建议,"
    "抑制追涨杀跌,不承诺收益,明确不确定性。"
)


def decide(
    profile: UserProfile,
    holdings: list[Holding],
    research: dict,
) -> list[Signal]:
    holdings_desc = [
        {"code": h.code, "name": h.name, "assetType": h.assetType, "amount": h.amount}
        for h in holdings
    ]
    prompt = f"""用户风险等级: {profile.riskLevel}; 阶段: {profile.ageStage or '未知'}。
持仓: {json.dumps(holdings_desc, ensure_ascii=False)}

研究结论:
{json.dumps(research, ensure_ascii=False, indent=2)}

请给出今日建议信号,每个标的一条,输出 JSON 数组:
[
  {{"target": "标的或资产", "action": "buy|hold|reduce|watch", "reason": "依据", "confidence": 0.0}}
]
action 必须是 buy/hold/reduce/watch 之一;confidence 为 0-1 的小数。
对风险等级低的用户更保守。"""
    raw = ask_json(prompt, model=DEEPSEEK_PRO_MODEL, system=SYSTEM)
    items = raw if isinstance(raw, list) else raw.get("signals", []) if isinstance(raw, dict) else []

    signals: list[Signal] = []
    for it in items:
        if not isinstance(it, dict):
            continue
        action = it.get("action", "watch")
        if action not in ("buy", "hold", "reduce", "watch"):
            action = "watch"
        try:
            confidence = float(it.get("confidence", 0.5))
        except (TypeError, ValueError):
            confidence = 0.5
        signals.append(
            Signal(
                target=str(it.get("target", "")),
                action=action,
                reason=str(it.get("reason", "")),
                confidence=max(0.0, min(1.0, confidence)),
            )
        )
    return signals


__all__ = ["decide"]
