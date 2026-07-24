# Injective 链上签文存证 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Anchor a sha256 hash of every MoneyGod multi-agent workflow's output (签文 + advice + report + disclaimer) as a memo on an Injective testnet transaction, so AI-generated financial advice becomes independently verifiable and tamper-evident — satisfying the Injective "AI x Blockchain" track's requirement for real, functioning chain integration.

**Architecture:** Two new deterministic-then-chain steps appended to the end of `research_workflow()` in `core-engine/src/workflow.py`, after the existing Report step: an `Advisor` step (already implemented in `src/advisor.py`, just needs wiring in) and a new `Notary` step (`src/notary.py`) that hashes the canonicalized result payload and broadcasts a self-send `MsgSend` with `memo="moneygod:v1:<hash>"` to Injective testnet via `pyinjective`. Chain failures are caught and degrade gracefully — they never block or corrupt the underlying financial analysis result.

**Tech Stack:** Python 3.12, `pyinjective` (official Injective Python SDK, PyPI package `injective-py`), `pytest` (new — project currently has zero automated tests), `httpx` (already a transitive dependency, used for the testnet faucet API call).

## Global Constraints

- `INJECTIVE_PRIVATE_KEY` (hex, no `0x` prefix) must never appear in a git commit, in conversation/log output, or in any file outside `.env` (local) / server `.env` (remote). Both are already gitignored.
- The wallet is testnet-only and holds no real value — generated fresh, never reused from any other chain/project.
- `notary.anchor()` must never raise — any failure (network, insufficient balance, timeout) returns `{"ok": False, "reason": "..."}` and the calling workflow must continue to return a complete, usable result.
- Injective testnet: `chain_id="injective-888"`, `fee_denom="inj"`, memo max length 256 chars (`pyinjective.constant.MAX_MEMO_CHARACTERS`).
- Production deploy target: `root@47.98.99.199`, service run via `setsid ... < /dev/null` (plain `nohup` alone leaves the SSH session hung — already hit this once).

---

### Task 1: Generate the Injective testnet wallet

**Files:**
- Create: `core-engine/scripts/generate_injective_wallet.py`
- Modify: `core-engine/requirements.txt`

**Interfaces:**
- Produces: a funded-later `inj1...` address and a hex private key, manually appended to `core-engine/.env` as `INJECTIVE_PRIVATE_KEY=<hex>` (not committed).

- [ ] **Step 1: Add the dependency**

Append to `core-engine/requirements.txt`:
```
injective-py>=1.16.0
```

Install it:
```bash
cd core-engine && .venv/bin/pip install -r requirements.txt
```
Expected: `injective-py` installs successfully (already verified working from the aliyun pip mirror in this environment).

- [ ] **Step 2: Write the wallet generation script**

Create `core-engine/scripts/generate_injective_wallet.py`:
```python
"""一次性生成 MoneyGod 专用的 Injective 测试网密钥对。

运行: .venv/bin/python scripts/generate_injective_wallet.py

只打印结果,不写文件。把打印出的 hex 私钥手动加进 .env 的
INJECTIVE_PRIVATE_KEY,私钥/助记词绝不能进 git、绝不能出现在日志里。
"""
from pyinjective.wallet import PrivateKey


def main() -> None:
    mnemonic, priv_key = PrivateKey.generate()
    address = priv_key.to_public_key().to_address().to_acc_bech32()
    print("=== MoneyGod Injective 测试网钱包(仅测试网用,不承载真实资产) ===")
    print("地址(inj1...):", address)
    print("私钥(hex, 加进 .env 的 INJECTIVE_PRIVATE_KEY):", priv_key.to_hex())
    print("助记词(仅备份用,不要提交/分享):", mnemonic)


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Run it and save the key**

```bash
cd core-engine && .venv/bin/python scripts/generate_injective_wallet.py
```
Expected output: three lines with an `inj1...` address (43 chars total), a 64-char hex string, and a 24-word mnemonic.

Verify the address shape without printing the private key again:
```bash
.venv/bin/python -c "
from pyinjective.wallet import PrivateKey
import re
mnemonic, pk = PrivateKey.generate()
addr = pk.to_public_key().to_address().to_acc_bech32()
assert re.match(r'^inj1[02-9ac-hj-np-z]{38}\$', addr), addr
print('address shape OK:', addr)
"
```
Expected: `address shape OK: inj1...`

Manually append the real generated hex key from Step 3's first run to `core-engine/.env`:
```
INJECTIVE_PRIVATE_KEY=<the hex string printed above, no 0x prefix>
```

- [ ] **Step 4: Commit the script (not the key)**

```bash
git add core-engine/scripts/generate_injective_wallet.py core-engine/requirements.txt
git commit -m "Add Injective testnet wallet generation script"
```

---

### Task 2: Fund the wallet via the testnet faucet

**Files:**
- Create: `core-engine/scripts/faucet_claim.py`

**Interfaces:**
- Consumes: the `inj1...` address from Task 1.
- Produces: a funded testnet wallet (verified via `fetch_bank_balance`), required by Task 4's live broadcast test.

- [ ] **Step 1: Write the faucet claim script**

Create `core-engine/scripts/faucet_claim.py`:
```python
"""尝试自动向 Injective 官方测试网水龙头 API 领取测试币,并轮询余额直到到账。

运行: .venv/bin/python scripts/faucet_claim.py <inj1...地址>

官方水龙头是批处理的(文档写 5-10 分钟),所以这里最多轮询 12 分钟。
如果超时还没到账,大概率是该地址/IP 被限流或需要走网页表单,
打印出网页地址,请用户手动领取。
"""
import asyncio
import sys
import time

import httpx

from pyinjective.async_client_v2 import AsyncClient
from pyinjective.core.network import Network

FAUCET_URL = "https://jsbqfdd4yk.execute-api.us-east-1.amazonaws.com/v1/faucet"
FAUCET_WEB_URL = "https://testnet.faucet.injective.network/"
POLL_INTERVAL_SECONDS = 30
POLL_TIMEOUT_SECONDS = 12 * 60


async def _balance(address: str) -> int:
    client = AsyncClient(network=Network.testnet())
    result = await client.fetch_bank_balance(address, "inj")
    return int(result.get("balance", {}).get("amount", "0"))


def main() -> None:
    if len(sys.argv) != 2:
        print("用法: faucet_claim.py <inj1...地址>")
        sys.exit(1)
    address = sys.argv[1]

    print(f"尝试调用官方测试网水龙头 API 给 {address} 领水...")
    try:
        resp = httpx.post(FAUCET_URL, json={"address": address}, timeout=15.0)
        print(f"faucet API 响应: {resp.status_code} {resp.text[:300]}")
    except Exception as e:  # noqa: BLE001
        print(f"faucet API 调用失败: {e}")

    print(f"轮询余额,最多等待 {POLL_TIMEOUT_SECONDS // 60} 分钟...")
    deadline = time.monotonic() + POLL_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        bal = asyncio.run(_balance(address))
        if bal > 0:
            print(f"已到账,余额 = {bal} inj(base unit)")
            return
        print(f"暂未到账,{POLL_INTERVAL_SECONDS} 秒后重试...")
        time.sleep(POLL_INTERVAL_SECONDS)

    print("自动领取超时,未检测到余额。请手动领取:")
    print(f"  1. 打开 {FAUCET_WEB_URL}")
    print(f"  2. 粘贴地址: {address}")
    print("  3. 领取后重新运行本脚本确认到账")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run it**

```bash
cd core-engine && .venv/bin/python scripts/faucet_claim.py <inj1... address from Task 1>
```
Expected: either `已到账,余额 = ... inj(base unit)` within ~12 minutes, or a manual-fallback message with the faucet web URL.

If manual fallback triggers: **stop and ask the user** to open `https://testnet.faucet.injective.network/`, paste the address, claim, then re-run this script to confirm the balance before continuing to Task 4.

- [ ] **Step 3: Commit the script**

```bash
git add core-engine/scripts/faucet_claim.py
git commit -m "Add Injective testnet faucet claim/poll script"
```

---

### Task 3: `notary.py` — canonical payload hashing (TDD, pure functions)

**Files:**
- Create: `core-engine/src/notary.py`
- Create: `core-engine/tests/__init__.py`
- Create: `core-engine/tests/test_notary.py`
- Create: `core-engine/requirements-dev.txt`

**Interfaces:**
- Produces: `notary._canonical_payload(result: dict) -> dict`, `notary._hash_payload(payload: dict) -> str` — used internally by `notary.anchor()` in Task 4.

- [ ] **Step 1: Add pytest as a dev dependency**

Create `core-engine/requirements-dev.txt`:
```
-r requirements.txt
pytest>=8.0.0
```
```bash
cd core-engine && .venv/bin/pip install -r requirements-dev.txt
```

- [ ] **Step 2: Write the failing test**

Create `core-engine/tests/__init__.py` (empty file).

Create `core-engine/tests/test_notary.py`:
```python
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
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
cd core-engine && .venv/bin/python -m pytest tests/test_notary.py -v
```
Expected: `ModuleNotFoundError: No module named 'src.notary'` (or collection error) — fails because `src/notary.py` doesn't exist yet.

- [ ] **Step 4: Implement the pure functions**

Create `core-engine/src/notary.py`:
```python
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
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
cd core-engine && .venv/bin/python -m pytest tests/test_notary.py -v
```
Expected: 3 passed.

- [ ] **Step 6: Commit**

```bash
git add core-engine/requirements-dev.txt core-engine/tests/__init__.py core-engine/tests/test_notary.py core-engine/src/notary.py
git commit -m "Add notary.py canonical payload hashing (TDD, unit tested)"
```

---

### Task 4: `notary.anchor()` — broadcast the memo transaction (integration-verified)

**Files:**
- Modify: `core-engine/src/notary.py`
- Create: `core-engine/scripts/notary_smoke_test.py`

**Interfaces:**
- Consumes: `_canonical_payload`, `_hash_payload` from Task 3; `INJECTIVE_PRIVATE_KEY` env var from Task 1; a funded wallet from Task 2.
- Produces: `notary.anchor(result: dict) -> dict` returning `{"ok": True, "txHash": str, "explorerUrl": str, "hash": str}` or `{"ok": False, "reason": str}`. Consumed by Task 5.

This step is an I/O boundary (real network broadcast) — it is integration-tested against live Injective testnet rather than unit-tested with a mock, since the whole point is a real, independently-verifiable transaction.

- [ ] **Step 1: Implement `anchor()`**

Append to `core-engine/src/notary.py`:
```python
import os
import time

from pyinjective.async_client_v2 import AsyncClient
from pyinjective.composer_v2 import Composer
from pyinjective.core.broadcaster import SimulatedTransactionFeeCalculator
from pyinjective.core.network import Network
from pyinjective.transaction import Transaction
from pyinjective.wallet import PrivateKey

_EXPLORER_TX_URL = "https://testnet.explorer.injective.network/transaction/{}"
_BROADCAST_TIMEOUT_SECONDS = 10


def _get_private_key() -> PrivateKey:
    hex_key = os.environ.get("INJECTIVE_PRIVATE_KEY")
    if not hex_key:
        raise RuntimeError("缺少 INJECTIVE_PRIVATE_KEY,请在 .env 中配置。")
    return PrivateKey.from_hex(hex_key)


async def _broadcast_memo(memo: str) -> str:
    """构建、签名、广播一笔带 memo 的自转账 MsgSend,返回 txhash。"""
    network = Network.testnet()
    client = AsyncClient(network=network)
    priv_key = _get_private_key()
    pub_key = priv_key.to_public_key()
    address = pub_key.to_address().to_acc_bech32()

    await client.sync_timeout_height()
    await client.fetch_account(address)

    composer = Composer(network=network.string())
    msg = composer.msg_send(from_address=address, to_address=address, amount=1, denom="inj")

    transaction = Transaction()
    transaction.with_messages(msg)
    transaction.with_sequence(client.get_sequence())
    transaction.with_account_num(client.get_number())
    transaction.with_chain_id(network.chain_id)
    transaction.with_memo(memo)

    fee_calculator = SimulatedTransactionFeeCalculator(client=client, composer=composer)
    await fee_calculator.configure_gas_fee_for_transaction(
        transaction=transaction, private_key=priv_key, public_key=pub_key
    )

    sign_doc = transaction.get_sign_doc(pub_key)
    signature = priv_key.sign(sign_doc.SerializeToString())
    tx_bytes = transaction.get_tx_data(signature, pub_key)

    result = await client.broadcast_tx_sync_mode(tx_bytes)
    tx_response = result.get("txResponse", {})
    code = tx_response.get("code", 0)
    if code:
        raise RuntimeError(f"广播失败,code={code}: {tx_response.get('rawLog', '')[:200]}")
    return tx_response["txhash"]


def anchor(result: dict) -> dict:
    """把 result 的关键字段哈希锚定到 Injective 测试网。任何失败都不抛出。"""
    import asyncio

    payload = _canonical_payload(result)
    content_hash = _hash_payload(payload)
    memo = f"{MEMO_PREFIX}{content_hash}"

    try:
        tx_hash = asyncio.run(asyncio.wait_for(_broadcast_memo(memo), timeout=_BROADCAST_TIMEOUT_SECONDS))
    except Exception as e:  # noqa: BLE001 链上存证失败不能影响主流程
        return {"ok": False, "reason": str(e)[:200], "hash": content_hash}

    return {
        "ok": True,
        "txHash": tx_hash,
        "explorerUrl": _EXPLORER_TX_URL.format(tx_hash),
        "hash": content_hash,
    }


__all__ = ["_canonical_payload", "_hash_payload", "anchor", "MEMO_PREFIX"]
```

- [ ] **Step 2: Write the live smoke test script**

Create `core-engine/scripts/notary_smoke_test.py`:
```python
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
```

- [ ] **Step 3: Run it against real testnet**

```bash
cd core-engine && .venv/bin/python scripts/notary_smoke_test.py
```
Expected: `"ok": true` with a real `txHash` and `explorerUrl`. If `"ok": false` with an insufficient-funds-style reason, go back to Task 2 and confirm the faucet claim actually landed (`fetch_bank_balance`) before retrying.

Open the printed `explorerUrl` and manually confirm the transaction's memo field equals `moneygod:v1:<expected_hash>` printed above.

- [ ] **Step 4: Commit**

```bash
git add core-engine/src/notary.py core-engine/scripts/notary_smoke_test.py
git commit -m "Implement notary.anchor(): broadcast memo-hash to Injective testnet"
```

---

### Task 5: Wire Advisor + Notary into the workflow

**Files:**
- Modify: `core-engine/src/workflow.py`

**Interfaces:**
- Consumes: `advisor.to_advice(result: dict) -> dict` (already implemented), `notary.anchor(result: dict) -> dict` (Task 4).
- Produces: `research_workflow()` now returns a dict with `result["advice"]` and `result["onchain"]` populated, and two additional trace entries (`Advisor`, `Notary`).

- [ ] **Step 1: Read the current end of `research_workflow()`**

```bash
grep -n "report_text = report_agent.write\|return {" core-engine/src/workflow.py
```
Locate the block that builds `report_text`, calls `compliance.ensure_disclaimer`, appends the `Report` trace step, and the final `return {...}` dict.

- [ ] **Step 2: Add the two new steps before the final return**

In `core-engine/src/workflow.py`, add near the top:
```python
from . import advisor, notary
```

Immediately after the existing `step("Report", "汇总为可解释投研报告(含风险提示)", {"chars": len(report_text)})` line and before the final `return {...}` dict, insert:
```python
    advice = advisor.to_advice(
        {"backtest": final_bt, "benchmark": benchmark_result, "risk": risk_result, "beatBenchmark": beat}
    )
    step("Advisor", "生成签文与大白话建议(纯规则,不调用 LLM)", advice)

    onchain_payload = {
        "query": query,
        "riskLevel": risk_level,
        "advice": advice,
        "report": report_text,
        "backtest": final_bt,
        "disclaimer": compliance.RISK_DISCLAIMER,
    }
    onchain = notary.anchor(onchain_payload)
    if onchain.get("ok"):
        step("Notary", "签文哈希锚定 Injective 测试网", {"txHash": onchain["txHash"]})
    else:
        step("Notary", "上链存证失败(不影响本次分析结果)", {"reason": onchain.get("reason")})
```

Then add `"advice": advice,` and `"onchain": onchain,` as new keys in the final `return {...}` dict, alongside the existing `"report": report_text,` and `"disclaimer": compliance.RISK_DISCLAIMER,` keys.

- [ ] **Step 3: Verify locally**

```bash
cd core-engine && timeout 120 .venv/bin/python -c "
from src.tasks import analyze_structured
from src.schemas import UserProfile
r = analyze_structured('用 600519.SH 000858.SZ 构建稳健的多因子策略并回测', UserProfile(userId='t', riskLevel='balanced'))
print('advice:', r.get('advice'))
print('onchain:', r.get('onchain'))
print('trace agents:', [t['agent'] for t in r.get('trace', [])])
" 2>&1 | grep -v "^INFO:httpx"
```
Expected: `advice` is a populated dict, `onchain` has `"ok": True` with a `txHash` (or a graceful `False`/`reason` if the faucet balance ran out — re-run Task 2's poll script to check), and `trace agents` ends with `[..., 'Report', 'Advisor', 'Notary']`.

- [ ] **Step 4: Commit**

```bash
git add core-engine/src/workflow.py
git commit -m "Wire Advisor and Notary agents into research_workflow()"
```

---

### Task 6: Clean up `tasks.py` and render the on-chain status line

**Files:**
- Modify: `core-engine/src/tasks.py`

**Interfaces:**
- Consumes: `result["onchain"]` (Task 5).

- [ ] **Step 1: Remove the now-duplicate `advisor.to_advice()` calls**

In `core-engine/src/tasks.py`, find the two call sites added previously (inside `handle_task` and `analyze_structured`) that look like:
```python
            from . import advisor
            from .workflow import research_workflow

            result = research_workflow(text, risk_level=profile.riskLevel)
            result["advice"] = advisor.to_advice(result)
            return _render_workflow(result)  # 报告内已含风险提示
```
Replace both with (removing the now-redundant `advisor` import and the manual `to_advice` call, since `research_workflow()` sets `result["advice"]` itself now):
```python
            from .workflow import research_workflow

            result = research_workflow(text, risk_level=profile.riskLevel)
            return _render_workflow(result)  # 报告内已含风险提示
```
And in `analyze_structured`, the equivalent block becomes:
```python
            from .workflow import research_workflow

            result = research_workflow(text, risk_level=riskLevel)
            return result
```

- [ ] **Step 2: Add the on-chain status line to `_render_workflow()`**

In `core-engine/src/tasks.py`, find `_render_fortune()` (added previously) and add a sibling function right after it:
```python
def _render_onchain(onchain: dict) -> str:
    if onchain.get("ok"):
        return f"⛓️ 链上存证:已锚定 Injective 测试网,交易哈希 {onchain.get('txHash')}({onchain.get('explorerUrl')})"
    return f"⛓️ 链上存证:本次未能上链,不影响分析结果({onchain.get('reason', '未知原因')})"
```
Then in `_render_workflow()`, insert `_render_onchain(r.get("onchain") or {})` into the `sections` list right after the existing `_render_fortune(r.get("advice") or {})` line.

- [ ] **Step 3: Verify**

```bash
cd core-engine && timeout 120 .venv/bin/python -c "
from src.tasks import handle_task
from src.schemas import UserProfile
text = handle_task('用 600519.SH 000858.SZ 构建稳健的多因子策略并回测', UserProfile(userId='t', riskLevel='balanced'))
print(text[:500])
" 2>&1 | grep -v "^INFO:httpx"
```
Expected: output includes a `🧧 今日签文:` line immediately followed by a `⛓️ 链上存证:` line before the `🤝 多 Agent 协作轨迹` section.

- [ ] **Step 4: Commit**

```bash
git add core-engine/src/tasks.py
git commit -m "Render on-chain attestation status in A2A text output; dedupe advisor call"
```

---

### Task 7: Web UI — show the on-chain module

**Files:**
- Modify: `core-engine/src/a2a_server/web/index.html`

- [ ] **Step 1: Add an on-chain card renderer**

In `core-engine/src/a2a_server/web/index.html`, find the `fortuneCard(adv)` function (added previously) and add a sibling function right after it:
```js
function onchainCard(onchain){
  const c = el('div','card');
  if(onchain && onchain.ok){
    c.innerHTML = `<h2>⛓️ 链上存证</h2>
      <div>已锚定 Injective 测试网,交易哈希:
        <a href="${esc(onchain.explorerUrl)}" target="_blank" rel="noopener">${esc(onchain.txHash)}</a>
      </div>`;
  } else {
    c.innerHTML = `<h2>⛓️ 链上存证</h2>
      <div class="muted">本次未能上链存证,不影响分析结果${onchain && onchain.reason ? '('+esc(onchain.reason)+')' : ''}</div>`;
  }
  return c;
}
```

- [ ] **Step 2: Insert it into `renderWorkflow()`**

Find the line `box.appendChild(fortuneCard(data.advice||{}));` inside `renderWorkflow(data)` and add immediately after it:
```js
  box.appendChild(onchainCard(data.onchain));
```

- [ ] **Step 3: Verify locally**

```bash
cd core-engine && nohup .venv/bin/python serve.py > /tmp/serve-local-test.log 2>&1 &
sleep 3
curl -s -X POST http://localhost:8000/api/analyze -H 'Content-Type: application/json' \
  -d '{"text":"用 600519.SH 000858.SZ 构建稳健的多因子策略并回测","riskLevel":"balanced"}' \
  | python3 -c "import json,sys; d=json.load(sys.stdin); print('onchain' in d, d.get('onchain'))"
kill %1 2>/dev/null
```
Expected: `True {'ok': True, 'txHash': '...', ...}` (or a graceful `False` dict).

- [ ] **Step 4: Commit**

```bash
git add core-engine/src/a2a_server/web/index.html
git commit -m "Add on-chain attestation card to Web UI"
```

---

### Task 8: Update the Agent Card description

**Files:**
- Modify: `core-engine/src/a2a_server/card.py`

- [ ] **Step 1: Extend the multi_agent_quant_research skill description**

In `core-engine/src/a2a_server/card.py`, find the `AgentSkill(id="multi_agent_quant_research", ...)` entry's `description` string. Append this sentence to the end of the existing description string (keep everything else unchanged):
```
"结论哈希锚定在 Injective 测试网(memo 存证),任何人都能独立复算哈希与链上记录比对,确保输出不可篡改。"
```

- [ ] **Step 2: Verify**

```bash
cd core-engine && .venv/bin/python -c "
from src.a2a_server.card import build_agent_card
card = build_agent_card()
skill = next(s for s in card.skills if s.id == 'multi_agent_quant_research')
assert 'Injective' in skill.description
print('OK:', skill.description[-80:])
"
```
Expected: `OK: ...`

- [ ] **Step 3: Commit**

```bash
git add core-engine/src/a2a_server/card.py
git commit -m "Mention Injective on-chain attestation in Agent Card skill description"
```

---

### Task 9: Deploy to the production server and verify publicly

**Files:**
- Modify: `core-engine/.env.example`

- [ ] **Step 1: Add the placeholder env var**

Append to `core-engine/.env.example`:
```
# Injective 测试网密钥(hex,无 0x 前缀),专款专用,仅用于签文上链存证
INJECTIVE_PRIVATE_KEY=在这里粘贴测试网专用私钥
```
```bash
git add core-engine/.env.example
git commit -m "Document INJECTIVE_PRIVATE_KEY in .env.example"
git push
```

- [ ] **Step 2: Sync code to the server**

```bash
rsync -az --exclude '.venv' --exclude '__pycache__' --exclude '*.pyc' --exclude '.env' \
  /data/projects/hackthon/moneyGod/core-engine/ root@47.98.99.199:/opt/moneygod/core-engine/
```
Expected: exits 0.

- [ ] **Step 3: Sync the private key to the server's `.env`**

```bash
ssh root@47.98.99.199 "grep -q INJECTIVE_PRIVATE_KEY /opt/moneygod/core-engine/.env || echo needs_append"
```
If it prints `needs_append`, append the same real key value from Task 1 (do not print it in this session — construct the remote append via a single non-echoing command):
```bash
ssh root@47.98.99.199 "cat >> /opt/moneygod/core-engine/.env" < <(grep INJECTIVE_PRIVATE_KEY /data/projects/hackthon/moneyGod/core-engine/.env)
```
Verify without printing the value:
```bash
ssh root@47.98.99.199 "grep -c INJECTIVE_PRIVATE_KEY /opt/moneygod/core-engine/.env"
```
Expected: `1`

- [ ] **Step 4: Install the new dependency on the server**

```bash
ssh root@47.98.99.199 "cd /opt/moneygod/core-engine && .venv/bin/pip install -r requirements.txt -q && echo INSTALL_OK"
```
Expected: `INSTALL_OK`

- [ ] **Step 5: Restart the service (using the setsid pattern that avoids the SSH hang)**

```bash
ssh -o ConnectTimeout=6 root@47.98.99.199 "lsof -ti:8000 -sTCP:LISTEN | xargs -r kill; sleep 1; echo stopped"
ssh -o ConnectTimeout=6 root@47.98.99.199 "cd /opt/moneygod/core-engine && \
A2A_PUBLIC_URL=http://47.98.99.199:8000 HOST=0.0.0.0 PORT=8000 \
setsid nohup .venv/bin/python serve.py > /var/log/moneygod.log 2>&1 < /dev/null & disown; sleep 3; echo STARTED"
```
Expected: `stopped` then `STARTED`, and this command must return promptly (not hang) — if it does hang again, it's cosmetic (the earlier hang did not affect a fresh follow-up `curl` check).

- [ ] **Step 6: Verify publicly end-to-end**

```bash
curl -s -m 8 -o /dev/null -w "healthz: %{http_code}\n" http://47.98.99.199:8000/healthz
curl -s -m 120 -X POST http://47.98.99.199:8000/api/analyze -H 'Content-Type: application/json' \
  -d '{"text":"用 600519.SH 000858.SZ 601318.SH 构建稳健的多因子策略并回测","riskLevel":"balanced"}' \
  | python3 -c "import json,sys; d=json.load(sys.stdin); print('advice:', d.get('advice')); print('onchain:', d.get('onchain'))"
```
Expected: `healthz: 200`, a populated `advice` dict, and `onchain` with `"ok": True` and a real `txHash` — open its `explorerUrl` once more to confirm the transaction is visible on the public testnet explorer.

- [ ] **Step 7: Final commit/push check**

```bash
git status --short
git log --oneline -10
```
Expected: clean working tree, all 8 prior task commits present, already pushed to `https://github.com/yidianyiko/moneyGod`.
