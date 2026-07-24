"""Factor 模块 —— 从 panda_data 拉取真实因子数据,确定性计算因子分值。

数据来源(已实测可用):
- get_factor: close / market_cap / net_profit / revenue / turnover / volume(价量 + 基础基本面)
- get_fina_performance: roe_weighted / net_profit_parent_yoy / operating_revenue_yoy(成长质量)

因子定义(全部由代码计算,横截面 z-score 标准化,方向统一为「越大越好」):
- momentum : 区间动量(近 N 日累计收益)
- value    : 估值(市值/净利 的倒数,越便宜分越高)
- quality  : 质量(ROE)
- growth   : 成长(净利同比 + 营收同比)
- low_vol  : 低波动(年化波动率取负)
"""
from __future__ import annotations

import datetime as _dt
import math
import os

_PANEL_FACTORS = ["close", "market_cap", "net_profit", "turnover"]

_inited = False


def _ensure_init() -> None:
    global _inited
    if _inited:
        return
    import panda_data

    username = os.environ.get("PANDA_USERNAME")
    password = os.environ.get("PANDA_PASSWORD")
    if not username or not password:
        raise RuntimeError("缺少 PANDA_USERNAME / PANDA_PASSWORD。")
    panda_data.init_token(username=username, password=password)
    _inited = True


def _date_range(lookback_days: int) -> tuple[str, str]:
    import panda_data

    end = panda_data.get_last_trade_date()
    end_dt = _dt.datetime.strptime(end, "%Y%m%d")
    start_dt = end_dt - _dt.timedelta(days=lookback_days)
    return start_dt.strftime("%Y%m%d"), end


def fetch_price_series(codes: list[str], lookback_days: int = 180) -> dict[str, list[float]]:
    """拉取升序收盘价序列 {code: [close,...]}。用于回测与动量。"""
    _ensure_init()
    import panda_data

    start, end = _date_range(lookback_days)
    df = panda_data.get_factor(
        symbol=codes, start_date=start, end_date=end, factors=["close"]
    )
    out: dict[str, list[float]] = {}
    if df is None or df.empty:
        return out
    for code in codes:
        sub = df[df["symbol"] == code].sort_values("date")
        out[code] = [float(x) for x in sub["close"].tolist()]
    return out


def _fundamentals(code: str) -> dict:
    """取最近一期财务表现(ROE / 同比)。失败返回空。"""
    import panda_data

    try:
        df = panda_data.get_fina_performance(symbol=code)
        if df is None or df.empty:
            return {}
        row = df.iloc[0]

        def g(k):
            try:
                v = float(row[k])
                return v if not math.isnan(v) else None
            except (KeyError, TypeError, ValueError):
                return None

        return {
            "roe": g("roe_weighted"),
            "net_profit_yoy": g("net_profit_parent_yoy"),
            "revenue_yoy": g("operating_revenue_yoy"),
        }
    except Exception:  # noqa: BLE001
        return {}


def raw_factors(codes: list[str], lookback_days: int = 180) -> dict[str, dict]:
    """每个标的的原始因子值(未标准化)。数字全部由代码计算。"""
    _ensure_init()
    import panda_data

    start, end = _date_range(lookback_days)
    df = panda_data.get_factor(
        symbol=codes, start_date=start, end_date=end, factors=_PANEL_FACTORS
    )
    result: dict[str, dict] = {}
    if df is None or df.empty:
        return {c: {} for c in codes}

    for code in codes:
        sub = df[df["symbol"] == code].sort_values("date")
        if sub.empty:
            result[code] = {}
            continue
        closes = [float(x) for x in sub["close"].tolist()]
        rets = [
            closes[i] / closes[i - 1] - 1.0
            for i in range(1, len(closes))
            if closes[i - 1]
        ]
        # 动量:区间累计收益
        momentum = (closes[-1] / closes[0] - 1.0) if len(closes) >= 2 and closes[0] else 0.0
        # 年化波动
        n = len(rets)
        vol = 0.0
        if n >= 2:
            mean = sum(rets) / n
            var = sum((r - mean) ** 2 for r in rets) / (n - 1)
            vol = math.sqrt(var) * math.sqrt(252)
        mkt_cap = float(sub["market_cap"].iloc[-1]) if "market_cap" in sub else None
        net_profit = float(sub["net_profit"].iloc[-1]) if "net_profit" in sub else None
        # 估值代理:市值 / 净利(越小越便宜);value 因子取其倒数
        pe_proxy = None
        if mkt_cap and net_profit and net_profit > 0:
            pe_proxy = mkt_cap / net_profit
        fund = _fundamentals(code)
        result[code] = {
            "last_close": closes[-1] if closes else None,
            "momentum": momentum,
            "volatility": vol,
            "pe_proxy": pe_proxy,
            "roe": fund.get("roe"),
            "net_profit_yoy": fund.get("net_profit_yoy"),
            "revenue_yoy": fund.get("revenue_yoy"),
            "n_days": len(closes),
        }
    return result


def _zscores(values: dict[str, float]) -> dict[str, float]:
    """横截面 z-score;仅对非 None 项计算,缺失记 0。"""
    items = [(k, v) for k, v in values.items() if v is not None]
    if len(items) < 2:
        return {k: 0.0 for k in values}
    vals = [v for _, v in items]
    mean = sum(vals) / len(vals)
    var = sum((v - mean) ** 2 for v in vals) / len(vals)
    sd = math.sqrt(var)
    out = {k: 0.0 for k in values}
    if sd == 0:
        return out
    for k, v in items:
        out[k] = (v - mean) / sd
    return out


# 因子权重(方向已统一为越大越好);可被策略层覆盖
DEFAULT_WEIGHTS = {
    "momentum": 0.30,
    "value": 0.20,
    "quality": 0.25,
    "growth": 0.15,
    "low_vol": 0.10,
}


def score_universe(
    codes: list[str],
    lookback_days: int = 180,
    weights: dict[str, float] | None = None,
) -> dict:
    """对候选池做多因子横截面打分。返回 raw + 标准化 + 综合分 + 排名。"""
    weights = weights or DEFAULT_WEIGHTS
    raw = raw_factors(codes, lookback_days)

    momentum = {c: raw[c].get("momentum") for c in codes}
    # value = 便宜程度 = -pe_proxy(pe 越小越好 → 取负)
    value = {c: (-raw[c]["pe_proxy"] if raw[c].get("pe_proxy") else None) for c in codes}
    quality = {c: raw[c].get("roe") for c in codes}
    growth = {}
    for c in codes:
        a, b = raw[c].get("net_profit_yoy"), raw[c].get("revenue_yoy")
        parts = [x for x in (a, b) if x is not None]
        growth[c] = sum(parts) / len(parts) if parts else None
    low_vol = {c: (-raw[c]["volatility"] if raw[c].get("volatility") is not None else None) for c in codes}

    z = {
        "momentum": _zscores(momentum),
        "value": _zscores(value),
        "quality": _zscores(quality),
        "growth": _zscores(growth),
        "low_vol": _zscores(low_vol),
    }

    composite = {}
    for c in codes:
        composite[c] = sum(weights.get(f, 0.0) * z[f][c] for f in z)

    ranked = sorted(codes, key=lambda c: composite[c], reverse=True)
    return {
        "weights": weights,
        "raw": raw,
        "zscores": {c: {f: round(z[f][c], 3) for f in z} for c in codes},
        "composite": {c: round(composite[c], 4) for c in codes},
        "ranking": ranked,
    }


__all__ = [
    "fetch_price_series",
    "raw_factors",
    "score_universe",
    "DEFAULT_WEIGHTS",
]
