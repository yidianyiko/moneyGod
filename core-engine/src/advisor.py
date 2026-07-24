"""M4 投顾表达层 —— 把量化结论翻译成"签文 + 大白话建议"。

对齐《模块拆分.md》AdviceResult 契约。纯确定性函数,不调用 LLM:
action 判定完全由真实回测/风险数字决定;签文文案从预置文案池按 action 选取,
避免生成式文本引入违规措辞或与数字矛盾的表述。
"""
from __future__ import annotations

_SIGNATURE = {
    "buy": "顺势而为 · 财气渐旺",
    "hold": "静观其变 · 守成待时",
    "reduce": "见好就收 · 减仓避险",
    "watch": "风云未定 · 观望为上",
}

_ACTION_CN = {"buy": "宜进", "hold": "宜守", "reduce": "宜减", "watch": "宜观"}


def _derive_action(backtest: dict, risk: dict, beat_benchmark: bool) -> str:
    if not backtest.get("ok"):
        return "watch"
    sharpe = backtest.get("sharpe", -99)
    mdd = backtest.get("max_drawdown", 0.0)
    risk_level = risk.get("risk_level", "medium") if risk.get("ok") else "medium"
    if risk_level == "high" or mdd < -0.30:
        return "reduce"
    if beat_benchmark and sharpe > 0:
        return "buy"
    if sharpe > -0.5:
        return "hold"
    return "watch"


def to_advice(workflow_result: dict) -> dict:
    """把 research_workflow() 的产出翻译成用户可读的签文层。纯函数,不调用 LLM。"""
    bt = workflow_result.get("backtest") or {}
    bm = workflow_result.get("benchmark") or {}
    risk = workflow_result.get("risk") or {}
    beat = bool(workflow_result.get("beatBenchmark"))

    action = _derive_action(bt, risk, beat)

    if bt.get("ok"):
        ann = bt.get("annualized_return") or 0.0
        bm_ann = bm.get("annualized_return") or 0.0
        beat_txt = "跑赢" if beat else "未跑赢"
        plain = (
            f"本次策略年化收益 {ann:+.1%},{beat_txt}买入持有基准"
            f"({bm_ann:+.1%}),组合风险等级「{risk.get('risk_level', '未知')}」。"
        )
    else:
        plain = "本次数据样本不足,未能完成完整回测,建议以保守观望为主。"

    return {
        "signature": _SIGNATURE[action],
        "plainAdvice": plain,
        "actionHint": action,
        "actionHintCn": _ACTION_CN[action],
    }


__all__ = ["to_advice"]
