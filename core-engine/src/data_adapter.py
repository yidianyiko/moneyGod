"""M3 数据适配层 —— PandaAI panda_data 实盘版。

MVP 范围:A股(stock) + 场内基金(ETF/LOF, get_fund_daily)。
原则:所有数值由本层用真实行情确定性计算,LLM 只负责解释,不负责编造数字。

关键事实(已用 panda_data 0.0.12 实测):
- get_market_data(type=...) 只支持 stock / future / index,不支持 fund。
- 基金走 get_fund_daily(仅覆盖场内 ETF/LOF;场外 OF 可能返回空)。
- 自定义因子库(get_factor)因子名非标准,MVP 不依赖,改为自行计算动量/回撤/区间位置。
- 返回的 DataFrame 默认按日期倒序,使用前统一升序排列。
"""
from __future__ import annotations

import datetime as _dt
import os

from .schemas import Holding

DATA_SOURCE = "panda_data:0.0.12"

# 大盘代表指数
_INDICES = {
    "000001.SH": "上证指数",
    "000300.SH": "沪深300",
    "399006.SZ": "创业板指",
}

_inited = False


def _ensure_init() -> None:
    """惰性初始化 panda_data 鉴权(账号密码来自环境变量)。"""
    global _inited
    if _inited:
        return
    import panda_data

    username = os.environ.get("PANDA_USERNAME")
    password = os.environ.get("PANDA_PASSWORD")
    if not username or not password:
        raise RuntimeError(
            "缺少 PANDA_USERNAME / PANDA_PASSWORD,请在 core-engine/.env 中配置。"
        )
    panda_data.init_token(username=username, password=password)
    _inited = True


def _date_range(lookback_days: int = 120) -> tuple[str, str]:
    """返回 (start, end) 的 YYYYMMDD 字符串,end 为最近交易日。"""
    import panda_data

    end = panda_data.get_last_trade_date()  # 'YYYYMMDD'
    end_dt = _dt.datetime.strptime(end, "%Y%m%d")
    start_dt = end_dt - _dt.timedelta(days=lookback_days)
    return start_dt.strftime("%Y%m%d"), end


def _fmt_date(yyyymmdd: str) -> str:
    return f"{yyyymmdd[:4]}-{yyyymmdd[4:6]}-{yyyymmdd[6:]}"


def _metrics_from_closes(closes: list[float]) -> dict:
    """由收盘价序列(升序)确定性计算行情指标。"""
    metrics: dict = {}
    if not closes:
        return metrics
    latest = closes[-1]
    metrics["close"] = round(latest, 4)
    if len(closes) >= 2:
        prev = closes[-2]
        metrics["chg_pct"] = round((latest / prev - 1) * 100, 2) if prev else 0.0
    # 区间动量:窗口首末收益
    first = closes[0]
    metrics["momentum_pct"] = round((latest / first - 1) * 100, 2) if first else 0.0
    # 最大回撤
    peak = closes[0]
    max_dd = 0.0
    for c in closes:
        if c > peak:
            peak = c
        if peak:
            dd = (c / peak - 1) * 100
            if dd < max_dd:
                max_dd = dd
    metrics["max_drawdown_pct"] = round(max_dd, 2)
    # 区间位置:最新价在窗口高低点之间的分位(0=最低,1=最高)
    lo, hi = min(closes), max(closes)
    metrics["range_position"] = round((latest - lo) / (hi - lo), 2) if hi > lo else 0.5
    return metrics


def _history(df, n: int = 30) -> list[dict]:
    """返回末 n 条 (date, close),升序,供前端画迷你走势图。"""
    tail = df.tail(n)
    out: list[dict] = []
    for _, row in tail.iterrows():
        out.append({"date": str(row["date"]), "close": round(float(row["close"]), 4)})
    return out


def get_market_overview() -> dict:
    """大盘 / 市场概览(实盘,确定性计算)。"""
    _ensure_init()
    import panda_data

    start, end = _date_range(lookback_days=90)
    indices: dict = {}
    for code, name in _INDICES.items():
        try:
            df = panda_data.get_index_daily(symbol=[code], start_date=start, end_date=end)
            if df is None or df.empty:
                continue
            df = df.sort_values("date")
            closes = [float(x) for x in df["close"].tolist()]
            m = _metrics_from_closes(closes)
            indices[name] = {
                "code": code,
                "close": m.get("close"),
                "chg_pct": m.get("chg_pct"),
                "momentum_90d_pct": m.get("momentum_pct"),
                "range_position": m.get("range_position"),
                "history": _history(df, 30),
            }
        except Exception as e:  # noqa: BLE001 单个指数失败不阻断整体
            indices[name] = {"code": code, "error": str(e)[:120]}

    # 确定性情绪判定:多数指数上涨/下跌
    ups = sum(1 for v in indices.values() if isinstance(v.get("chg_pct"), (int, float)) and v["chg_pct"] > 0)
    total = sum(1 for v in indices.values() if isinstance(v.get("chg_pct"), (int, float)))
    if total == 0:
        sentiment = "行情数据暂缺,建议以持仓自身基本面为主。"
    elif ups == total:
        sentiment = "主要指数普涨,市场情绪偏暖。"
    elif ups == 0:
        sentiment = "主要指数普跌,市场情绪偏谨慎。"
    else:
        sentiment = "主要指数涨跌分化,结构性行情为主。"

    return {
        "date": _fmt_date(end),
        "indices": indices,
        "sentiment": sentiment,
        "source": DATA_SOURCE,
    }


def _fetch_stock(code: str, start: str, end: str) -> dict:
    import panda_data

    snap: dict = {"code": code, "assetType": "stock"}
    df = panda_data.get_market_data(symbol=[code], start_date=start, end_date=end, type="stock")
    if df is not None and not df.empty:
        df = df.sort_values("date")
        closes = [float(x) for x in df["close"].tolist()]
        snap.update(_metrics_from_closes(closes))
        snap["history"] = _history(df, 30)
        if "name" in df.columns:
            snap["market_name"] = str(df["name"].iloc[-1])
    # 行业归属
    try:
        ind = panda_data.get_stock_industry(stock_symbol=code)
        if ind is not None and not ind.empty:
            snap["industry"] = str(ind["industry_name"].iloc[0])
    except Exception:  # noqa: BLE001
        pass
    return snap


def _fetch_fund(code: str, start: str, end: str) -> dict:
    import panda_data

    snap: dict = {"code": code, "assetType": "fund"}
    try:
        df = panda_data.get_fund_daily(symbol=[code], start_date=start, end_date=end)
        if df is not None and not df.empty:
            df = df.sort_values("date")
            closes = [float(x) for x in df["close"].tolist()]
            snap.update(_metrics_from_closes(closes))
            snap["history"] = _history(df, 30)
        else:
            snap["note"] = "场内行情为空(可能为场外 OF 基金,MVP 数据不覆盖)。"
    except Exception as e:  # noqa: BLE001
        snap["note"] = f"基金行情获取失败: {str(e)[:120]}"
    return snap


def get_holdings_snapshot(holdings: list[Holding]) -> list[dict]:
    """给定持仓,返回每个标的的实盘行情 / 确定性指标快照。"""
    _ensure_init()
    start, end = _date_range(lookback_days=120)

    snapshots: list[dict] = []
    for h in holdings:
        base = {"code": h.code, "name": h.name, "assetType": h.assetType, "amount": h.amount}
        if h.assetType == "cash":
            snapshots.append({**base, "note": "现金/货币,视为低风险仓位。", "source": DATA_SOURCE})
            continue
        try:
            if h.assetType == "fund":
                snap = _fetch_fund(h.code, start, end)
            else:
                snap = _fetch_stock(h.code, start, end)
        except Exception as e:  # noqa: BLE001 单标的失败不阻断
            snap = {"code": h.code, "assetType": h.assetType, "error": str(e)[:150]}
        snapshots.append({**base, **snap, "source": DATA_SOURCE})
    return snapshots


__all__ = ["get_market_overview", "get_holdings_snapshot", "DATA_SOURCE"]
