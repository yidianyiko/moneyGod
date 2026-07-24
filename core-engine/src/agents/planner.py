"""Planner Agent —— 理解意图,拆解当日分析任务。

用低延时 Seed 模型做轻量规划;返回一个 focus 列表指导后续分析。
"""
from __future__ import annotations

from ..llm import SEED_MODEL, ask_json
from ..schemas import Holding, UserProfile

SYSTEM = "你是量化投研团队的任务规划者(Planner)。只做规划,不下投资结论。"


def plan(profile: UserProfile, holdings: list[Holding]) -> dict:
    holdings_desc = ", ".join(f"{h.name}({h.assetType})" for h in holdings) or "无持仓"
    prompt = f"""用户风险等级: {profile.riskLevel}; 阶段: {profile.ageStage or '未知'}。
当前持仓: {holdings_desc}。

请规划"每日理财抽签"需要分析的重点,输出 JSON:
{{
  "focus": ["要分析的重点1", "重点2"],
  "need_market": true,
  "need_holdings": true
}}"""
    try:
        result = ask_json(prompt, model=SEED_MODEL, system=SYSTEM)
        if isinstance(result, dict):
            result.setdefault("focus", [])
            result.setdefault("need_market", True)
            result.setdefault("need_holdings", bool(holdings))
            return result
    except Exception:  # noqa: BLE001 - 规划失败时回退到默认计划
        pass
    return {"focus": ["大盘趋势", "持仓风险"], "need_market": True, "need_holdings": bool(holdings)}


__all__ = ["plan"]
