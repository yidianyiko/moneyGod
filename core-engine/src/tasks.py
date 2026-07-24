"""A2A 通用任务入口 —— 把平台发来的自然语言任务转成可解释的量化分析报告。

设计:
- 若任务里出现 A股/基金代码 → 走"标的分析"流水线(真实数据 + Research + Strategy)。
- 否则 → 走"市场问答",以真实大盘概览为事实锚点,由 DeepSeek 解释。
- 所有输出统一经 compliance 清洗并附风险提示。
- 数字全部由 data_adapter 用真实行情确定性计算,LLM 只做解释。
"""
from __future__ import annotations

import json
import re

from . import compliance, data_adapter
from .llm import DEEPSEEK_PRO_MODEL, ask
from .schemas import Holding, UserProfile

# 匹配带后缀(600519.SH)或裸 6 位代码
_CODE_RE = re.compile(r"\b(\d{6})(?:\.(SH|SZ|OF))?\b", re.IGNORECASE)

_FUND_PREFIX = {"51", "56", "58", "50", "15", "16", "18", "12", "13"}


def _guess_asset(code6: str) -> str:
    return "fund" if code6[:2] in _FUND_PREFIX else "stock"


def _guess_suffix(code6: str) -> str:
    if code6[0] in {"6", "5", "9"} or code6[:2] in {"11", "20"}:
        return "SH"
    return "SZ"


def _normalize(code6: str, suffix: str | None) -> str:
    suffix = (suffix or _guess_suffix(code6)).upper()
    return f"{code6}.{suffix}"


def extract_targets(text: str) -> list[Holding]:
    """从自然语言里抽取标的,转成 Holding(amount=0 表示仅分析、非持仓)。"""
    seen: dict[str, Holding] = {}
    for m in _CODE_RE.finditer(text):
        code6, suffix = m.group(1), m.group(2)
        asset = "fund" if (suffix and suffix.upper() == "OF") else _guess_asset(code6)
        code = _normalize(code6, suffix)
        if code not in seen:
            seen[code] = Holding(assetType=asset, code=code, name=code, amount=0.0)
    return list(seen.values())


def _default_profile() -> UserProfile:
    return UserProfile(userId="a2a-task", riskLevel="balanced", ageStage=None)


# 触发多 Agent 投研工作流(带回测反馈闭环)的意图关键词
_WORKFLOW_KW = (
    "策略", "回测", "因子", "组合", "选股", "多因子", "配置", "alpha", "portfolio",
    "strategy", "backtest", "factor", "投研", "夏普", "sharpe", "优化",
)


def _wants_workflow(text: str, targets: list[Holding]) -> bool:
    """含标的代码,或出现策略/回测/因子等意图 → 走多 Agent 工作流。"""
    if targets:
        return True
    low = text.lower()
    return any(k.lower() in low for k in _WORKFLOW_KW)


def _render_trace(trace: list[dict]) -> str:
    lines = ["🤝 多 Agent 协作轨迹(过程可解释)"]
    for i, t in enumerate(trace, 1):
        lines.append(f"  {i}. [{t['agent']}] {t['action']}")
    return "\n".join(lines)


def _render_backtest(bt: dict, bench: dict, beat: bool, iters: int) -> str:
    if not bt.get("ok"):
        return "📉 回测:样本不足,未能完成。"
    lines = [f"📉 策略回测(自算,数字可复现;经 {iters} 轮反馈优化)"]
    lines.append(
        f"  · 年化 {bt.get('annualized_return')} | 夏普 {bt.get('sharpe')} | "
        f"最大回撤 {bt.get('max_drawdown')} | 胜率 {bt.get('win_rate')}"
    )
    if bench.get("ok"):
        lines.append(
            f"  · 基准(等权买入持有):年化 {bench.get('annualized_return')} | "
            f"夏普 {bench.get('sharpe')} | 回撤 {bench.get('max_drawdown')}"
        )
        lines.append(f"  · 是否跑赢基准(风险调整后):{'是' if beat else '否'}")
    return "\n".join(lines)


def _render_workflow(r: dict) -> str:
    params = r.get("strategyParams", {})
    sections = [
        f"财神 MoneyGod · 多 Agent 量化投研\n任务:{r.get('query', '').strip()}",
        _render_trace(r.get("trace", [])),
        "🔬 因子排名(横截面多因子打分):" + " > ".join(r.get("factor", {}).get("ranking", [])),
        f"🧭 最终策略参数:因子权重 {params.get('weights')} | 持仓数 {params.get('top_k')} | "
        f"调仓 {params.get('rebalance')}日 | 回看 {params.get('lookback_days')}日",
        _render_backtest(r.get("backtest", {}), r.get("benchmark", {}), r.get("beatBenchmark", False), r.get("iterations", 1)),
        "📊 组合风险:等级 " + str(r.get("risk", {}).get("risk_level"))
        + f" | 波动 {r.get('risk', {}).get('annualized_volatility')} | 回撤 {r.get('risk', {}).get('max_drawdown')}",
        "📝 投研报告\n" + r.get("report", ""),
        "📌 数据来源:" + ", ".join(r.get("sources", [])),
    ]
    return "\n\n".join(sections)


_QA_SYSTEM = (
    "你是严谨的投研助手'财神 MoneyGod'。基于给定的真实大盘数据做客观解释与教育性说明,"
    "不做绝对收益承诺,不用'稳赚/必涨/保本'等措辞,明确不确定性。"
    "回答简洁、结构化、可解释。"
)


def _answer_qa(text: str) -> str:
    market = data_adapter.get_market_overview()
    prompt = (
        f"用户问题:{text}\n\n"
        f"可参考的真实大盘数据(务必据此解释,不要编造数字):\n"
        f"{json.dumps(market, ensure_ascii=False, indent=2)}\n\n"
        f"请给出客观、结构化的解释性回答。"
    )
    body = ask(prompt, model=DEEPSEEK_PRO_MODEL, system=_QA_SYSTEM)
    return f"财神 MoneyGod · 市场问答\n\n{body}\n\n📌 数据来源:{market.get('source', '')}"


def handle_task(text: str, profile: UserProfile | None = None) -> str:
    """A2A 通用任务入口:自然语言进,可解释报告出(已含风险提示)。"""
    profile = profile or _default_profile()
    text = (text or "").strip()
    if not text:
        return compliance.ensure_disclaimer("未收到有效任务内容,请提供分析标的或市场问题。")

    try:
        targets = extract_targets(text)
        if _wants_workflow(text, targets):
            from .workflow import research_workflow

            result = research_workflow(text, risk_level=profile.riskLevel)
            return _render_workflow(result)  # 报告内已含风险提示
        body = _answer_qa(text)
    except Exception as e:  # noqa: BLE001 顶层兜底,保证 A2A 始终有响应
        body = (
            "抱歉,分析过程中数据或模型调用出现异常,已转为保守中性提示。\n"
            f"（技术原因:{str(e)[:200]}）\n"
            "建议:市场存在不确定性时,优先控制仓位、分散配置、保留流动性。"
        )
    return compliance.ensure_disclaimer(body)


def analyze_structured(text: str, profile: UserProfile | None = None) -> dict:
    """给 Web UI 用的结构化结果(含真实行情、走势、信号、风险)。

    与 handle_task 共用同一套引擎与数据,区别只是返回结构化 dict 供前端渲染。
    """
    profile = profile or _default_profile()
    text = (text or "").strip()
    if not text:
        return {
            "type": "empty",
            "task": text,
            "disclaimer": compliance.RISK_DISCLAIMER,
        }

    riskLevel = profile.riskLevel
    try:
        targets = extract_targets(text)
        if _wants_workflow(text, targets):
            from .workflow import research_workflow

            return research_workflow(text, risk_level=riskLevel)
        # 无标的且非策略意图:市场问答
        market = data_adapter.get_market_overview()
        prompt = (
            f"用户问题:{text}\n\n真实大盘数据(据此解释,勿编造数字):\n"
            f"{json.dumps(market, ensure_ascii=False, indent=2)}\n\n请给出客观、结构化的解释性回答。"
        )
        answer = compliance.scrub(ask(prompt, model=DEEPSEEK_PRO_MODEL, system=_QA_SYSTEM))
        return {
            "type": "qa",
            "task": text,
            "riskLevel": riskLevel,
            "market": market,
            "answer": answer,
            "sources": [market.get("source", "")],
            "disclaimer": compliance.RISK_DISCLAIMER,
        }
    except Exception as e:  # noqa: BLE001
        return {
            "type": "error",
            "task": text,
            "message": f"分析过程出现异常,已转为保守中性提示。技术原因:{str(e)[:200]}",
            "disclaimer": compliance.RISK_DISCLAIMER,
        }


__all__ = ["handle_task", "analyze_structured", "extract_targets"]
