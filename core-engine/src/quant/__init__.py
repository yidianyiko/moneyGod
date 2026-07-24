"""确定性量化核心。所有数字由代码计算,LLM 只解释不编造。"""
from . import backtest, factors, metrics, risk

__all__ = ["metrics", "factors", "backtest", "risk"]
