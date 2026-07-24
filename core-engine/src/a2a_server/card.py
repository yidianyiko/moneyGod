"""Agent Card 定义 —— 对外声明能力,供 A2A 平台发现与调用。

PandaAI 硬要求:可公开访问的 Agent Card;底座模型 DeepSeek;输出含风险提示。
"""
from __future__ import annotations

import os

from a2a.types import AgentCapabilities, AgentCard, AgentProvider, AgentSkill

# 部署后对外可访问的服务根 URL,通过环境变量注入(如 https://xxx.ngrok.app)
PUBLIC_BASE_URL = os.environ.get("A2A_PUBLIC_URL", "http://localhost:8000")

_SKILLS = [
    AgentSkill(
        id="multi_agent_quant_research",
        name="多 Agent 量化投研(因子→策略→回测反馈闭环)",
        description=(
            "一支协作式 AI 投研团队:Planner 拆解任务 → Factor 多因子横截面打分 → "
            "Strategy 生成可回测的策略参数 → Backtest 用真实行情自算回测(年化/夏普/回撤/胜率) → "
            "若不达标则把回测反馈打回 Strategy 重新生成(最多 3 轮) → Risk 评估组合风险 → "
            "Report 汇总为可解释投研报告。所有绩效数字由确定性引擎计算,LLM 只做参数设计与解释。"
            "结论哈希锚定在 Injective 测试网(memo 存证),任何人都能独立复算哈希与链上记录比对,确保输出不可篡改。"
        ),
        tags=["Multi-Agent", "因子研究", "策略回测", "投研", "反馈闭环", "A股"],
        examples=[
            "用 600519.SH 000858.SZ 601318.SH 构建稳健的多因子策略并回测",
            "给 600036.SH 000333.SZ 做因子分析、策略回测和风险评估",
            "帮我设计一个低波动多因子选股策略并验证是否跑赢基准",
        ],
    ),
    AgentSkill(
        id="portfolio_analysis",
        name="标的/组合量化分析",
        description=(
            "对给定的一个或多个 A股/基金代码做多因子解读:动量、估值、质量、成长、低波因子打分,"
            "组合风险画像(波动/最大回撤/集中度),并给出可解释结论与风险点。数字全部由代码计算。"
        ),
        tags=["投研", "组合分析", "风险", "因子"],
        examples=[
            "分析 000001.SZ 和 600036.SH 的因子表现与风险",
            "600519.SH 的动量和波动特征如何?",
        ],
    ),
    AgentSkill(
        id="market_qa",
        name="市场问答",
        description=(
            "以真实大盘指数数据(上证/沪深300/创业板)为事实锚点,回答宏观与市场相关的"
            "教育性、解释性问题,不做收益承诺。"
        ),
        tags=["市场", "宏观", "投教"],
        examples=[
            "今天 A股 大盘怎么样?",
            "现在市场情绪偏乐观还是谨慎?",
        ],
    ),
]


def build_agent_card(base_url: str | None = None) -> AgentCard:
    base = (base_url or PUBLIC_BASE_URL).rstrip("/")
    return AgentCard(
        name="财神 MoneyGod",
        description=(
            "财神 MoneyGod 是一支'玄学皮·量化芯'的多 Agent AI 投研团队:Planner/Factor/Strategy/"
            "Backtest/Risk/Report 协作完成投研任务,核心是 Backtest→Strategy 的回测反馈闭环——"
            "策略不达标会被真实回测数据打回重新生成。所有绩效数字由确定性引擎计算,LLM 仅负责"
            "参数设计与解释;每条输出均含风险提示,不做任何收益承诺。底座模型为 DeepSeek。"
        ),
        url=f"{base}/",
        version="0.2.0",
        protocol_version="0.2.6",
        preferred_transport="JSONRPC",
        provider=AgentProvider(
            organization="AdventureX 2026 · MoneyGod Team",
            url=base,
        ),
        capabilities=AgentCapabilities(
            streaming=False,
            push_notifications=False,
            state_transition_history=False,
        ),
        default_input_modes=["text/plain"],
        default_output_modes=["text/plain"],
        skills=_SKILLS,
    )


__all__ = ["build_agent_card", "PUBLIC_BASE_URL"]
