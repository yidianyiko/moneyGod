"""跑通一次每日抽签。运行: python main.py"""
import json

from src.engine import daily_draw
from src.schemas import Holding, UserProfile


def main() -> None:
    profile = UserProfile(
        userId="demo-user",
        riskLevel="conservative",
        ageStage="职场新人",
        monthlyBudget=3000,
        goals=["攒钱", "稳健增值"],
    )
    holdings = [
        Holding(assetType="fund", code="510300.SH", name="沪深300ETF", amount=5000, cost=5200),
        Holding(assetType="stock", code="600519.SH", name="贵州茅台", amount=8000, cost=7500),
        Holding(assetType="cash", code="CASH", name="活期", amount=2000),
    ]

    conclusion = daily_draw(profile, holdings)
    print(json.dumps(conclusion.to_dict(), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
