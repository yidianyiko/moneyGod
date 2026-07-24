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
