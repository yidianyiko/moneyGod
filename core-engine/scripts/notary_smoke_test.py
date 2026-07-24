"""对 notary.anchor() 做一次真实的 Injective 测试网广播,人工核实。

运行: .venv/bin/python scripts/notary_smoke_test.py
前提: .env 里 INJECTIVE_PRIVATE_KEY 已配置且钱包已通过 Task 2 领到测试币。
"""
import json

from src.notary import anchor, _canonical_payload, _hash_payload

FAKE_RESULT = {
    "query": "smoke test",
    "riskLevel": "balanced",
    "advice": {"signature": "静观其变 · 守成待时", "plainAdvice": "test", "actionHint": "hold", "actionHintCn": "宜守"},
    "report": "smoke test report",
    "backtest": {"ok": True, "sharpe": 0.1, "annualized_return": 0.01, "max_drawdown": -0.05},
    "disclaimer": "test disclaimer",
}


def main() -> None:
    expected_hash = _hash_payload(_canonical_payload(FAKE_RESULT))
    print("期望的哈希:", expected_hash)

    outcome = anchor(FAKE_RESULT)
    print(json.dumps(outcome, ensure_ascii=False, indent=2))

    if outcome.get("ok"):
        assert outcome["hash"] == expected_hash, "返回的 hash 和本地重算不一致!"
        print("✅ 广播成功,请打开", outcome["explorerUrl"], "手动核对 memo 字段是否为:")
        print(f"   moneygod:v1:{expected_hash}")
    else:
        print("❌ 广播失败:", outcome.get("reason"))


if __name__ == "__main__":
    main()
