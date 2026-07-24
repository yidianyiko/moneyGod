"""Report Agent —— 把各 Agent 的确定性产出汇总成可解释投研报告。

严格约束:所有数字来自上游确定性模块,LLM 只做组织与解释,不新增/修改任何数值。
"""
from __future__ import annotations

import json

from ..llm import DEEPSEEK_PRO_MODEL, ask

SYSTEM = (
    "你是投研报告撰写者(Report)。只依据给定的确定性数据组织成报告,"
    "不得新增或篡改任何数字,不使用'稳赚/必涨/保本'等违规措辞,明确风险与不确定性。"
)


def write(
    query: str,
    factor_result: dict,
    strategy_params: dict,
    backtest_result: dict,
    benchmark_result: dict,
    risk_result: dict,
    iterations: int,
) -> str:
    payload = {
        "用户需求": query,
        "因子排名": factor_result.get("ranking"),
        "因子综合分": factor_result.get("composite"),
        "最终策略参数": strategy_params,
        "策略回测": {
            k: backtest_result.get(k)
            for k in ("annualized_return", "sharpe", "max_drawdown", "win_rate", "calmar", "n_days")
        },
        "买入持有基准": benchmark_result,
        "组合风险": risk_result,
        "策略迭代轮数": iterations,
    }
    prompt = f"""以下是量化投研团队各环节的确定性产出(数字已固定,不可改动):

{json.dumps(payload, ensure_ascii=False, indent=2)}

请撰写一份简明中文投研报告(300-450字),结构:
1. 结论(策略是否跑赢基准、风险调整后表现如何,一句话点题)
2. 因子视角(排名靠前标的的驱动因子)
3. 策略与回测(参数含义 + 关键指标解读,说明经过了几轮优化)
4. 风险提示(结合回撤/波动/集中度)
用词客观克制,不承诺收益。直接输出报告正文,不要 JSON。"""
    return ask(prompt, model=DEEPSEEK_PRO_MODEL, system=SYSTEM)


__all__ = ["write"]
