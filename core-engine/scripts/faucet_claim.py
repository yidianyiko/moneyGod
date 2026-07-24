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
