"""核心数据结构。对齐《模块拆分.md》第 2 节的共享契约。

QuantConclusion 是引擎(M2/M3)的内部产出,交给 M4 投顾表达层翻译成用户可读的 AdviceResult。
"""
from __future__ import annotations

from dataclasses import asdict, dataclass, field
from typing import Literal

RiskLevel = Literal["conservative", "balanced", "aggressive"]
Action = Literal["buy", "hold", "reduce", "watch"]
AssetType = Literal["stock", "fund", "cash"]


@dataclass
class UserProfile:
    userId: str
    riskLevel: RiskLevel = "balanced"
    ageStage: str | None = None       # 如 "职场新人"
    monthlyBudget: float | None = None  # 可投月结余
    goals: list[str] = field(default_factory=list)


@dataclass
class Holding:
    assetType: AssetType
    code: str
    name: str
    amount: float           # 持有金额
    cost: float | None = None  # 成本


@dataclass
class Signal:
    target: str             # 标的 / 资产
    action: Action
    reason: str             # 依据(因子 / 数据)
    confidence: float       # 0-1


@dataclass
class QuantConclusion:
    date: str
    marketSummary: str            # 市场概览(专业口径)
    signals: list[Signal] = field(default_factory=list)
    dataSources: list[str] = field(default_factory=list)  # 供合规溯源

    def to_dict(self) -> dict:
        return asdict(self)


__all__ = [
    "UserProfile",
    "Holding",
    "Signal",
    "QuantConclusion",
    "RiskLevel",
    "Action",
    "AssetType",
]
