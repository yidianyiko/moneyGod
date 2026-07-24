"""Multi-Agent 投研工作流编排(冲奖版核心)。

协作链路(带反馈闭环):
  Planner → Factor → Strategy → Backtest ─┐(不达标)
                         ▲                 │
                         └──── revise ◀─────┘  (≤ MAX_ITERS 轮)
  → Risk → Report

关键差异化:Backtest 用真实数据自算指标;若夏普低于阈值,Planner 判定不达标,
把真实回测反馈回传 Strategy 重新生成参数——这是"真协作",不是简单串联。
所有数字由 quant 模块确定性计算,LLM 只做参数设计与解释。
"""
from __future__ import annotations

from . import compliance
from .agents import report as report_agent
from .agents import strategy_quant
from .quant import backtest as bt_mod
from .quant import factors as factor_mod
from .quant import risk as risk_mod
from .tasks import extract_targets

# 冲奖版默认候选池:流动性好的宽基/龙头,便于回测(用户未给代码时兜底)
DEFAULT_UNIVERSE = ["600519.SH", "000858.SZ", "601318.SH", "600036.SH", "000333.SZ"]

SHARPE_TARGET = 0.5   # 风险调整后收益达标线
MAX_ITERS = 3         # 策略最多迭代轮数


def _risk_to_target(risk_level: str) -> float:
    return {"conservative": 0.8, "balanced": 0.5, "aggressive": 0.2}.get(risk_level, 0.5)


def research_workflow(query: str, risk_level: str = "balanced") -> dict:
    """执行完整多 Agent 投研工作流,返回结论 + 协作轨迹(trace)。"""
    trace: list[dict] = []

    def step(agent: str, action: str, output):
        trace.append({"agent": agent, "action": action, "output": output})

    # 1) Planner —— 解析候选池
    targets = extract_targets(query)
    universe = [h.code for h in targets] or list(DEFAULT_UNIVERSE)
    used_default = not targets
    step(
        "Planner",
        "解析任务意图,确定候选池",
        {"universe": universe, "used_default": used_default, "risk_level": risk_level},
    )

    # 2) Factor Agent —— 多因子横截面打分
    factor_result = factor_mod.score_universe(universe, lookback_days=180)
    step(
        "Factor",
        "多因子打分(动量/估值/质量/成长/低波),横截面 z-score",
        {"ranking": factor_result["ranking"], "composite": factor_result["composite"]},
    )

    # 3) Strategy Agent —— 生成可回测参数
    params = strategy_quant.propose(query, universe, risk_level)
    step("Strategy", "生成初始策略参数", params)

    # 4) Backtest Agent + 反馈闭环
    target_sharpe = _risk_to_target(risk_level)
    best = None
    iterations = 0
    for it in range(1, MAX_ITERS + 1):
        iterations = it
        bt = bt_mod.run_backtest(
            universe,
            weights=params["weights"],
            top_k=params["top_k"],
            rebalance=params["rebalance"],
            lookback_days=params["lookback_days"],
        )
        step(
            "Backtest",
            f"第 {it} 轮真实回测(数字由引擎计算)",
            {k: bt.get(k) for k in ("ok", "sharpe", "annualized_return", "max_drawdown", "win_rate")},
        )
        if not bt.get("ok"):
            best = (params, bt)
            break
        if best is None or bt.get("sharpe", -99) > best[1].get("sharpe", -99):
            best = (params, bt)
        if bt.get("sharpe", -99) >= target_sharpe:
            step("Planner", f"回测达标(夏普≥{target_sharpe}),闭环结束", {"passed": True})
            break
        if it < MAX_ITERS:
            step(
                "Planner",
                f"回测未达标(夏普 {bt.get('sharpe')} < {target_sharpe}),打回 Strategy 重生成",
                {"passed": False},
            )
            params = strategy_quant.revise(query, universe, risk_level, params, bt)
            step("Strategy", f"第 {it+1} 轮据反馈调整参数", params)

    final_params, final_bt = best
    benchmark_result = bt_mod.benchmark(universe)
    step(
        "Backtest",
        "买入持有基准对照",
        benchmark_result,
    )
    beat = (
        final_bt.get("ok")
        and benchmark_result.get("ok")
        and final_bt.get("sharpe", -99) > benchmark_result.get("sharpe", -99)
    )

    # 5) Risk Agent
    risk_result = risk_mod.portfolio_risk(universe)
    step(
        "Risk",
        "组合风险画像(波动/回撤/集中度)+ 合规红线",
        {k: risk_result.get(k) for k in ("risk_level", "max_drawdown", "annualized_volatility", "flags")},
    )

    # 6) Report Agent
    report_text = report_agent.write(
        query, factor_result, final_params, final_bt, benchmark_result, risk_result, iterations
    )
    report_text = compliance.ensure_disclaimer(report_text)
    step("Report", "汇总为可解释投研报告(含风险提示)", {"chars": len(report_text)})

    return {
        "type": "multi_agent_research",
        "query": query,
        "riskLevel": risk_level,
        "universe": universe,
        "usedDefaultUniverse": used_default,
        "factor": factor_result,
        "strategyParams": final_params,
        "backtest": final_bt,
        "benchmark": benchmark_result,
        "beatBenchmark": bool(beat),
        "risk": risk_result,
        "iterations": iterations,
        "report": report_text,
        "trace": trace,
        "sources": ["panda_data:0.0.12", "self-computed-backtest", "multi-agent-workflow"],
        "disclaimer": compliance.RISK_DISCLAIMER,
    }


__all__ = ["research_workflow", "DEFAULT_UNIVERSE"]
