"""确定性回测引擎 —— 平台未提供回测 Skill,由本模块自算,数字可复现。

策略空间(供 Strategy Agent 通过参数表达,避免 LLM 编造数字):
- weights      : 多因子权重 dict(方向已统一为越大越好)
- top_k        : 每期持有综合分最高的 K 只(等权)
- rebalance    : 调仓周期(交易日)
- lookback_days: 打分回看窗口

回测方式:滚动窗口横截面打分 → 选 top_k 等权持有 → 到期用真实收盘价结算 → 拼接净值曲线。
所有指标(年化/夏普/回撤/胜率/换手)由 metrics.py 纯函数计算。
"""
from __future__ import annotations

from . import factors, metrics


def _rebalance_score(
    price_map: dict[str, list[float]],
    codes: list[str],
    end_idx: int,
    lookback: int,
    weights: dict[str, float],
) -> dict[str, float]:
    """基于截至 end_idx 的窗口做一次横截面打分(仅用价格动量+波动,避免前视)。

    注:基本面(ROE/成长)为最新快照,回测中作为静态因子处理;
    动量/低波按窗口内价格滚动计算,保证不使用未来数据。
    """
    import math

    mom, vol = {}, {}
    for c in codes:
        series = price_map.get(c, [])
        lo = max(0, end_idx - lookback)
        window = series[lo : end_idx + 1]
        if len(window) < 2 or not window[0]:
            mom[c] = None
            vol[c] = None
            continue
        mom[c] = window[-1] / window[0] - 1.0
        rets = [window[i] / window[i - 1] - 1.0 for i in range(1, len(window)) if window[i - 1]]
        if len(rets) >= 2:
            mean = sum(rets) / len(rets)
            var = sum((r - mean) ** 2 for r in rets) / (len(rets) - 1)
            vol[c] = math.sqrt(var)
        else:
            vol[c] = None

    zmom = factors._zscores(mom)
    zvol = factors._zscores({c: (-v if v is not None else None) for c, v in vol.items()})
    # 回测仅能对时序因子(动量/低波)做无前视滚动打分;估值/质量/成长为静态快照,
    # 不参与时序择时,故此处只按策略给定的 momentum/low_vol 权重结算。
    wm = weights.get("momentum", 0.0)
    wv = weights.get("low_vol", 0.0)
    if wm == 0.0 and wv == 0.0:
        wm = 1.0  # 两者都未指定时退化为纯动量,避免无排序依据
    return {c: wm * zmom[c] + wv * zvol[c] for c in codes}


def run_backtest(
    codes: list[str],
    weights: dict[str, float] | None = None,
    top_k: int = 1,
    rebalance: int = 5,
    lookback_days: int = 20,
    total_days: int = 180,
) -> dict:
    """跑一次确定性回测,返回指标 + 净值曲线 + 调仓记录。"""
    weights = weights or {"momentum": 0.7, "low_vol": 0.3}
    price_map = factors.fetch_price_series(codes, lookback_days=total_days)
    codes = [c for c in codes if len(price_map.get(c, [])) > lookback_days + rebalance]
    if not codes:
        return {"ok": False, "reason": "价格样本不足,无法回测。"}

    length = min(len(price_map[c]) for c in codes)
    top_k = max(1, min(top_k, len(codes)))

    port_returns: list[float] = []
    trades: list[dict] = []
    prev_hold: set[str] = set()
    turnover_events = 0

    start_idx = lookback_days
    i = start_idx
    while i + rebalance < length:
        scores = _rebalance_score(price_map, codes, i, lookback_days, weights)
        ranked = sorted(codes, key=lambda c: scores[c], reverse=True)
        hold = set(ranked[:top_k])
        if hold != prev_hold:
            turnover_events += 1
            trades.append({"day_idx": i, "hold": sorted(hold)})
            prev_hold = hold
        # 结算未来 rebalance 天的等权组合收益
        for step in range(1, rebalance + 1):
            day = i + step
            daily = []
            for c in hold:
                s = price_map[c]
                if day < len(s) and s[day - 1]:
                    daily.append(s[day] / s[day - 1] - 1.0)
            port_returns.append(sum(daily) / len(daily) if daily else 0.0)
        i += rebalance

    if not port_returns:
        return {"ok": False, "reason": "回测区间过短。"}

    eq = metrics.equity_curve(port_returns)
    ann = metrics.annualized_return(port_returns)
    mdd = metrics.max_drawdown(eq)
    result = {
        "ok": True,
        "params": {
            "weights": weights,
            "top_k": top_k,
            "rebalance": rebalance,
            "lookback_days": lookback_days,
        },
        "n_days": len(port_returns),
        "cumulative_return": round(metrics.cumulative_return(port_returns), 4),
        "annualized_return": round(ann, 4),
        "annualized_volatility": round(metrics.annualized_volatility(port_returns), 4),
        "sharpe": round(metrics.sharpe_ratio(port_returns), 3),
        "max_drawdown": round(mdd, 4),
        "win_rate": round(metrics.win_rate(port_returns), 3),
        "calmar": round(metrics.calmar_ratio(ann, mdd), 3),
        "rebalance_count": turnover_events,
        "equity_curve": [round(x, 4) for x in eq],
        "trades": trades[-10:],
    }
    return result


def benchmark(codes: list[str], total_days: int = 180) -> dict:
    """等权买入持有基准,用于对照策略是否跑赢。"""
    price_map = factors.fetch_price_series(codes, lookback_days=total_days)
    codes = [c for c in codes if len(price_map.get(c, [])) >= 2]
    if not codes:
        return {"ok": False}
    length = min(len(price_map[c]) for c in codes)
    port_returns = []
    for day in range(1, length):
        daily = []
        for c in codes:
            s = price_map[c]
            if s[day - 1]:
                daily.append(s[day] / s[day - 1] - 1.0)
        port_returns.append(sum(daily) / len(daily) if daily else 0.0)
    eq = metrics.equity_curve(port_returns)
    ann = metrics.annualized_return(port_returns)
    return {
        "ok": True,
        "cumulative_return": round(metrics.cumulative_return(port_returns), 4),
        "annualized_return": round(ann, 4),
        "sharpe": round(metrics.sharpe_ratio(port_returns), 3),
        "max_drawdown": round(metrics.max_drawdown(eq), 4),
    }


__all__ = ["run_backtest", "benchmark"]
