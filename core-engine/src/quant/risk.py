"""确定性风险模块 —— 组合层风险指标,数字由代码计算。

- 回撤 / 波动:来自 metrics
- 集中度:等权持仓的 HHI(赫芬达尔指数)近似
- 合规红线:回撤/波动阈值触发保守提示
"""
from __future__ import annotations

from . import factors, metrics


def portfolio_risk(codes: list[str], total_days: int = 180) -> dict:
    """等权组合的风险画像(买入持有口径)。"""
    price_map = factors.fetch_price_series(codes, lookback_days=total_days)
    valid = [c for c in codes if len(price_map.get(c, [])) >= 2]
    if not valid:
        return {"ok": False, "reason": "无足够价格样本。"}

    length = min(len(price_map[c]) for c in valid)
    port_returns = []
    for day in range(1, length):
        daily = [
            price_map[c][day] / price_map[c][day - 1] - 1.0
            for c in valid
            if price_map[c][day - 1]
        ]
        port_returns.append(sum(daily) / len(daily) if daily else 0.0)

    eq = metrics.equity_curve(port_returns)
    vol = metrics.annualized_volatility(port_returns)
    mdd = metrics.max_drawdown(eq)
    n = len(valid)
    hhi = round(sum((1.0 / n) ** 2 for _ in valid), 3)  # 等权 HHI = 1/n

    flags = []
    if mdd < -0.30:
        flags.append("区间最大回撤超过 30%,历史波动偏大。")
    if vol > 0.40:
        flags.append("年化波动率偏高,注意仓位控制。")
    if n == 1:
        flags.append("仅单一标的,缺乏分散,个股风险集中。")

    level = "high" if (mdd < -0.30 or vol > 0.40 or n == 1) else (
        "medium" if (mdd < -0.15 or vol > 0.25) else "low"
    )
    return {
        "ok": True,
        "n_holdings": n,
        "annualized_volatility": round(vol, 4),
        "max_drawdown": round(mdd, 4),
        "concentration_hhi": hhi,
        "risk_level": level,
        "flags": flags,
    }


__all__ = ["portfolio_risk"]
