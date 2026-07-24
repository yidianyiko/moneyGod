"""M2 核心引擎编排层。

daily_draw: Planner → 数据适配 → Research → Strategy → 组装 QuantConclusion。
QuantConclusion 交给 M4(投顾表达层)翻译成用户可读的签文与建议。
"""
from __future__ import annotations

from . import data_adapter
from .agents import planner, research, strategy
from .schemas import Holding, QuantConclusion, UserProfile


def daily_draw(profile: UserProfile, holdings: list[Holding]) -> QuantConclusion:
    plan = planner.plan(profile, holdings)

    market = data_adapter.get_market_overview() if plan.get("need_market", True) else {}
    snapshot = (
        data_adapter.get_holdings_snapshot(holdings)
        if plan.get("need_holdings", True) and holdings
        else []
    )

    analysis = research.analyze(plan.get("focus", []), market, snapshot)
    signals = strategy.decide(profile, holdings, analysis)

    sources = []
    if market:
        sources.append(market.get("source", data_adapter.DATA_SOURCE))
    if snapshot:
        sources.append(data_adapter.DATA_SOURCE)

    return QuantConclusion(
        date=market.get("date", "") or "",
        marketSummary=analysis.get("market_view", ""),
        signals=signals,
        dataSources=sorted(set(sources)),
    )


__all__ = ["daily_draw"]
