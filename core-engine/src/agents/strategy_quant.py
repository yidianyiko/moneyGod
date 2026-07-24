"""Strategy Agent(升级版)—— 把自然语言意图翻译成「可回测的策略参数」。

关键:LLM 不产出任何绩效数字,只产出策略参数(因子权重 / 持仓数 / 调仓周期)。
真实回测由 quant.backtest 执行。收到回测反馈后可据实调整参数(反馈闭环)。
"""
from __future__ import annotations

import json

from ..llm import DEEPSEEK_PRO_MODEL, ask_json

SYSTEM = (
    "你是量化策略工程师(Strategy)。你只输出策略参数,绝不编造收益/夏普等数字——"
    "那些由回测引擎计算。参数要可解释、克制,匹配用户风险等级。"
)

_FACTORS = ["momentum", "low_vol", "value", "quality", "growth"]


def _sanitize(params: dict, universe_size: int) -> dict:
    weights = params.get("weights") or {}
    clean_w = {}
    for f in _FACTORS:
        try:
            v = float(weights.get(f, 0.0))
        except (TypeError, ValueError):
            v = 0.0
        if v > 0:
            clean_w[f] = v
    if not clean_w:
        clean_w = {"momentum": 0.7, "low_vol": 0.3}
    total = sum(clean_w.values())
    clean_w = {k: round(v / total, 3) for k, v in clean_w.items()}

    def _int(key, default, lo, hi):
        try:
            return max(lo, min(hi, int(params.get(key, default))))
        except (TypeError, ValueError):
            return default

    return {
        "weights": clean_w,
        "top_k": _int("top_k", 1, 1, max(1, universe_size)),
        "rebalance": _int("rebalance", 5, 3, 20),
        "lookback_days": _int("lookback_days", 20, 10, 60),
        "rationale": str(params.get("rationale", ""))[:200],
    }


def propose(query: str, universe: list[str], risk_level: str) -> dict:
    """首次生成策略参数。"""
    prompt = f"""用户需求: {query}
风险等级: {risk_level}
候选池(股票代码): {universe}

请设计一个多因子选股策略,只输出参数 JSON:
{{
  "weights": {{"momentum": 0.5, "low_vol": 0.3, "value": 0.2}},
  "top_k": 1,
  "rebalance": 5,
  "lookback_days": 20,
  "rationale": "为什么这样配(≤50字)"
}}
可用因子: momentum(动量)/low_vol(低波)/value(便宜)/quality(ROE)/growth(成长)。
风险等级低 → 提高 low_vol/quality 权重、拉长 rebalance。"""
    try:
        raw = ask_json(prompt, model=DEEPSEEK_PRO_MODEL, system=SYSTEM)
        if isinstance(raw, dict):
            return _sanitize(raw, len(universe))
    except Exception:  # noqa: BLE001
        pass
    return _sanitize({}, len(universe))


def revise(query: str, universe: list[str], risk_level: str, prev_params: dict, bt: dict) -> dict:
    """根据回测反馈调整参数(反馈闭环)。"""
    prompt = f"""用户需求: {query}; 风险等级: {risk_level}; 候选池: {universe}

上一轮策略参数:
{json.dumps(prev_params, ensure_ascii=False)}

上一轮真实回测结果(由引擎计算,不可篡改):
夏普={bt.get('sharpe')}, 年化={bt.get('annualized_return')}, 最大回撤={bt.get('max_drawdown')}, 胜率={bt.get('win_rate')}

回测未达标(夏普偏低/回撤偏大)。请调整参数以改善风险调整后收益,只输出同结构 JSON。
重要:回测择时只由 momentum(动量)与 low_vol(低波)两个时序因子驱动,
value/quality/growth 为静态快照不参与择时。因此有效调整手段是:
- 改变 momentum 与 low_vol 的相对比例(如趋势差时提高 low_vol、趋势好时提高 momentum);
- 调整 top_k(降低集中度可减小回撤)、lookback_days、rebalance。
请确保本轮参数与上一轮有实质差异。"""
    try:
        raw = ask_json(prompt, model=DEEPSEEK_PRO_MODEL, system=SYSTEM)
        if isinstance(raw, dict):
            return _sanitize(raw, len(universe))
    except Exception:  # noqa: BLE001
        pass
    # 回退:确定性地更保守
    fallback = dict(prev_params)
    w = dict(fallback.get("weights", {}))
    w["low_vol"] = round(w.get("low_vol", 0.0) + 0.2, 3)
    fallback["weights"] = w
    fallback["lookback_days"] = min(60, prev_params.get("lookback_days", 20) + 10)
    return _sanitize(fallback, len(universe))


__all__ = ["propose", "revise"]
