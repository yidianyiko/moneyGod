"""A2A 服务装配 —— 组合 AgentCard + 执行器 + 任务存储,产出可托管的 Starlette 应用。

对外路由:
- GET  /                            (财神 MoneyGod 可视化网页)
- GET  /.well-known/agent-card.json (PandaAI 硬要求路径)
- GET  /.well-known/agent.json      (A2A SDK 默认路径,兼容)
- POST /                            (A2A JSON-RPC 端点:message/send, tasks/get 等)
- POST /api/analyze                 (Web UI 用的结构化分析接口)
- POST /api/fortune/draw            (赛博财神庙 T5AI 板端求签接口)
- GET  /healthz                     (存活探针)
"""
from __future__ import annotations

import asyncio
from pathlib import Path

from a2a.server.apps import A2AStarletteApplication
from a2a.server.request_handlers import DefaultRequestHandler
from a2a.server.tasks import InMemoryTaskStore
from starlette.applications import Starlette
from starlette.responses import FileResponse, JSONResponse
from starlette.routing import Route

from ..schemas import UserProfile
from ..tasks import analyze_structured
from ..config import get_fortune_token
from ..fortune import generate_fortune
from .card import build_agent_card
from .executor import MoneyGodAgentExecutor

PANDAAI_CARD_PATH = "/.well-known/agent-card.json"
_WEB_DIR = Path(__file__).parent / "web"
_VALID_RISK = {"conservative", "balanced", "aggressive"}


def build_app(base_url: str | None = None) -> Starlette:
    agent_card = build_agent_card(base_url)

    handler = DefaultRequestHandler(
        agent_executor=MoneyGodAgentExecutor(),
        task_store=InMemoryTaskStore(),
    )
    a2a_app = A2AStarletteApplication(agent_card=agent_card, http_handler=handler)

    # 以 PandaAI 要求的路径挂载卡片 + JSON-RPC 端点
    app: Starlette = a2a_app.build(agent_card_url=PANDAAI_CARD_PATH)

    card_json = agent_card.model_dump(mode="json", by_alias=True, exclude_none=True)

    async def compat_card(_request):
        return JSONResponse(card_json)

    async def healthz(_request):
        return JSONResponse({"status": "ok", "agent": agent_card.name})

    async def home(_request):
        return FileResponse(_WEB_DIR / "index.html")

    async def api_analyze(request):
        try:
            body = await request.json()
        except Exception:  # noqa: BLE001
            return JSONResponse({"type": "error", "message": "请求体需为 JSON"}, status_code=400)
        text = (body.get("text") or "").strip()
        risk = body.get("riskLevel") if body.get("riskLevel") in _VALID_RISK else "balanced"
        if not text:
            return JSONResponse({"type": "error", "message": "请输入标的代码或市场问题"}, status_code=400)
        profile = UserProfile(userId="web-ui", riskLevel=risk)
        result = await asyncio.to_thread(analyze_structured, text, profile)
        return JSONResponse(result)

    async def api_fortune_draw(request):
        token = get_fortune_token()
        if token and request.headers.get("X-Fortune-Token") != token:
            return JSONResponse({"error": "unauthorized"}, status_code=401)
        try:
            body = await request.json()
        except Exception:  # noqa: BLE001
            return JSONResponse({"error": "invalid json"}, status_code=400)
        category = str(body.get("category") or "今日运势")
        question = str(body.get("question") or "")
        answer = str(body.get("answer") or "")
        result = await asyncio.to_thread(generate_fortune, category, question, answer)
        return JSONResponse(result)

    # 兼容 SDK 默认路径 + 健康检查 + Web UI + 结构化 API
    app.router.routes.append(Route("/", home, methods=["GET"]))
    app.router.routes.append(Route("/.well-known/agent.json", compat_card, methods=["GET"]))
    app.router.routes.append(Route("/healthz", healthz, methods=["GET"]))
    app.router.routes.append(Route("/api/analyze", api_analyze, methods=["POST"]))
    app.router.routes.append(Route("/api/fortune/draw", api_fortune_draw, methods=["POST"]))
    return app


__all__ = ["build_app", "PANDAAI_CARD_PATH"]
