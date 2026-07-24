"""Research Agent —— 归因分析。

把市场概览 + 持仓快照喂给 DeepSeek-Pro,产出结构化的分析要点(非投资指令)。
"""
from __future__ import annotations

import json

from ..llm import DEEPSEEK_PRO_MODEL, ask_json

SYSTEM = (
    "你是严谨的量化研究员(Research)。基于给定数据做客观归因分析,"
    "不做绝对收益承诺,不使用'稳赚/必涨'等措辞。"
)


def analyze(focus: list[str], market: dict, holdings_snapshot: list[dict]) -> dict:
    prompt = f"""分析重点: {focus}

市场概览数据:
{json.dumps(market, ensure_ascii=False, indent=2)}

持仓快照数据:
{json.dumps(holdings_snapshot, ensure_ascii=False, indent=2)}

请基于以上数据做归因分析,输出 JSON:
{{
  "market_view": "对当前市场的一句话客观判断",
  "holding_notes": [
    {{"code": "标的代码", "note": "该标的的数据要点与风险"}}
  ],
  "key_risks": ["风险点1", "风险点2"]
}}"""
    result = ask_json(prompt, model=DEEPSEEK_PRO_MODEL, system=SYSTEM)
    if not isinstance(result, dict):
        result = {}
    result.setdefault("market_view", market.get("sentiment", ""))
    result.setdefault("holding_notes", [])
    result.setdefault("key_risks", [])
    return result


__all__ = ["analyze"]
