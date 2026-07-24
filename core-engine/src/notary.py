"""M6 链上存证层 —— 把工作流结论的哈希锚定到 Injective 测试网。

原则:哈希对象是确定性、key 排序的 JSON,任何人拿到 API 响应本身就能重新算出
同一个哈希、和链上 memo 对比,这是"可验证"的关键。链上广播失败绝不影响主流程,
一律 catch 后返回 {"ok": False, "reason": ...}。
"""
from __future__ import annotations

import hashlib
import json

MEMO_PREFIX = "moneygod:v1:"


def _canonical_payload(result: dict) -> dict:
    """抽取要存证的字段,构造确定性(key 排序)负载。"""
    bt = result.get("backtest") or {}
    return {
        "query": result.get("query", ""),
        "riskLevel": result.get("riskLevel", ""),
        "advice": result.get("advice") or {},
        "report": result.get("report", ""),
        "disclaimer": result.get("disclaimer", ""),
        "backtestSummary": {
            "ok": bt.get("ok"),
            "sharpe": bt.get("sharpe"),
            "annualizedReturn": bt.get("annualized_return"),
            "maxDrawdown": bt.get("max_drawdown"),
        },
    }


def _hash_payload(payload: dict) -> str:
    canonical_json = json.dumps(payload, sort_keys=True, ensure_ascii=False, separators=(",", ":"))
    return hashlib.sha256(canonical_json.encode("utf-8")).hexdigest()


__all__ = ["_canonical_payload", "_hash_payload", "MEMO_PREFIX"]
