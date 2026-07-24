from src.notary import _canonical_payload, _hash_payload


def _sample_result() -> dict:
    return {
        "query": "用 600519.SH 000858.SZ 构建稳健的多因子策略并回测",
        "riskLevel": "balanced",
        "advice": {
            "signature": "静观其变 · 守成待时",
            "plainAdvice": "本次策略年化收益 -12.0%,未跑赢买入持有基准(-5.0%),组合风险等级「medium」。",
            "actionHint": "hold",
            "actionHintCn": "宜守",
        },
        "report": "策略在90天回测期内未跑赢买入持有基准...",
        "backtest": {"ok": True, "sharpe": -1.2, "annualized_return": -0.12, "max_drawdown": -0.2},
        "disclaimer": "【风险提示】以上为基于历史行情数据的量化分析...",
    }


def test_canonical_payload_is_deterministic_regardless_of_key_order():
    result_a = _sample_result()
    result_b = {k: result_a[k] for k in reversed(list(result_a.keys()))}
    assert _canonical_payload(result_a) == _canonical_payload(result_b)


def test_hash_payload_is_stable_sha256_hex():
    payload = _canonical_payload(_sample_result())
    h1 = _hash_payload(payload)
    h2 = _hash_payload(payload)
    assert h1 == h2
    assert len(h1) == 64
    assert all(c in "0123456789abcdef" for c in h1)


def test_hash_changes_when_advice_changes():
    result = _sample_result()
    h1 = _hash_payload(_canonical_payload(result))
    result["advice"]["actionHint"] = "buy"
    h2 = _hash_payload(_canonical_payload(result))
    assert h1 != h2
