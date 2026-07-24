"""确定性绩效/风险指标 —— 纯函数,不依赖外部服务。

原则:所有数字由代码计算,可复现、可审计。LLM 不参与任何数值生成。
输入统一为「收益率序列」或「净值序列」,输出标准量化指标。
"""
from __future__ import annotations

import math

TRADING_DAYS = 252


def daily_returns(closes: list[float]) -> list[float]:
    """收盘价序列(升序)→ 日收益率序列。"""
    out: list[float] = []
    for i in range(1, len(closes)):
        prev = closes[i - 1]
        if prev:
            out.append(closes[i] / prev - 1.0)
        else:
            out.append(0.0)
    return out


def cumulative_return(returns: list[float]) -> float:
    """累计收益率。"""
    eq = 1.0
    for r in returns:
        eq *= 1.0 + r
    return eq - 1.0


def annualized_return(returns: list[float]) -> float:
    """年化收益率(几何)。"""
    if not returns:
        return 0.0
    total = cumulative_return(returns)
    years = len(returns) / TRADING_DAYS
    if years <= 0:
        return 0.0
    base = 1.0 + total
    if base <= 0:
        return -1.0
    return base ** (1.0 / years) - 1.0


def annualized_volatility(returns: list[float]) -> float:
    """年化波动率(样本标准差)。"""
    n = len(returns)
    if n < 2:
        return 0.0
    mean = sum(returns) / n
    var = sum((r - mean) ** 2 for r in returns) / (n - 1)
    return math.sqrt(var) * math.sqrt(TRADING_DAYS)


def sharpe_ratio(returns: list[float], risk_free: float = 0.0) -> float:
    """年化夏普比率(默认无风险利率 0)。"""
    n = len(returns)
    if n < 2:
        return 0.0
    daily_rf = risk_free / TRADING_DAYS
    excess = [r - daily_rf for r in returns]
    mean = sum(excess) / n
    var = sum((r - mean) ** 2 for r in excess) / (n - 1)
    sd = math.sqrt(var)
    if sd == 0:
        return 0.0
    return (mean / sd) * math.sqrt(TRADING_DAYS)


def max_drawdown(equity: list[float]) -> float:
    """净值序列 → 最大回撤(负数,如 -0.23)。"""
    if not equity:
        return 0.0
    peak = equity[0]
    mdd = 0.0
    for v in equity:
        if v > peak:
            peak = v
        if peak:
            dd = v / peak - 1.0
            if dd < mdd:
                mdd = dd
    return mdd


def equity_curve(returns: list[float], start: float = 1.0) -> list[float]:
    """收益率序列 → 净值曲线。"""
    eq = start
    curve = [start]
    for r in returns:
        eq *= 1.0 + r
        curve.append(eq)
    return curve


def win_rate(returns: list[float]) -> float:
    """胜率:正收益期数占比。"""
    if not returns:
        return 0.0
    wins = sum(1 for r in returns if r > 0)
    return wins / len(returns)


def calmar_ratio(ann_return: float, mdd: float) -> float:
    """Calmar = 年化收益 / |最大回撤|。"""
    if mdd == 0:
        return 0.0
    return ann_return / abs(mdd)


__all__ = [
    "TRADING_DAYS",
    "daily_returns",
    "cumulative_return",
    "annualized_return",
    "annualized_volatility",
    "sharpe_ratio",
    "max_drawdown",
    "equity_curve",
    "win_rate",
    "calmar_ratio",
]
