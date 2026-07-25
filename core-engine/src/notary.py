"""M6 链上存证层 —— 把工作流结论的哈希锚定到 Injective 测试网。

原则:哈希对象是确定性、key 排序的 JSON,任何人拿到 API 响应本身就能重新算出
同一个哈希、和链上 memo 对比,这是"可验证"的关键。链上广播失败绝不影响主流程,
一律 catch 后返回 {"ok": False, "reason": ...}。
"""
from __future__ import annotations

import hashlib
import json
import os
from decimal import Decimal

from pyinjective.async_client_v2 import AsyncClient
from pyinjective.composer_v2 import Composer
from pyinjective.core.broadcaster import SimulatedTransactionFeeCalculator
from pyinjective.core.network import Network
from pyinjective.transaction import Transaction
from pyinjective.wallet import PrivateKey

MEMO_PREFIX = "moneygod:v1:"
_EXPLORER_TX_URL = "https://testnet.explorer.injective.network/transaction/{}"
_BROADCAST_TIMEOUT_SECONDS = 30


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


def _get_private_key() -> PrivateKey:
    hex_key = os.environ.get("INJECTIVE_PRIVATE_KEY")
    if not hex_key:
        raise RuntimeError("缺少 INJECTIVE_PRIVATE_KEY,请在 .env 中配置。")
    return PrivateKey.from_hex(hex_key)


async def _broadcast_memo(memo: str) -> str:
    """构建、签名、广播一笔带 memo 的自转账 MsgSend,返回 txhash。"""
    network = Network.testnet()
    client = AsyncClient(network=network)
    try:
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

        fee_calculator = SimulatedTransactionFeeCalculator(
            client=client, composer=composer, gas_limit_adjustment_multiplier=Decimal("1.4")
        )
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
    finally:
        # AsyncClient opens a chain gRPC channel (and a background timeout-height
        # sync task) in __init__; this code path only ever talks over that channel,
        # so close it here to avoid leaking channels/tasks across anchor() calls
        # on a long-running server.
        await client.close_chain_channel()


def anchor(result: dict) -> dict:
    """把 result 的关键字段哈希锚定到 Injective 测试网。任何失败都不抛出。"""
    import asyncio

    content_hash = ""
    try:
        payload = _canonical_payload(result)
        content_hash = _hash_payload(payload)
        memo = f"{MEMO_PREFIX}{content_hash}"
        tx_hash = asyncio.run(asyncio.wait_for(_broadcast_memo(memo), timeout=_BROADCAST_TIMEOUT_SECONDS))
    except Exception as e:  # noqa: BLE001 链上存证失败不能影响主流程
        reason = str(e) or type(e).__name__
        return {"ok": False, "reason": reason[:200], "hash": content_hash}

    return {
        "ok": True,
        "txHash": tx_hash,
        "explorerUrl": _EXPLORER_TX_URL.format(tx_hash),
        "hash": content_hash,
    }


__all__ = ["_canonical_payload", "_hash_payload", "anchor", "MEMO_PREFIX"]
