"""HTTP 服务装配 —— 赛博财神庙 T5AI 板端求签接口。

对外路由:
- POST /api/fortune/draw            (板端求签:抽档 + 生成签文)
- POST /api/fortune/question        (结合签文生成解签是非题)
- POST /api/fortune/interpret       (结合是非回答生成针对性解签)
- POST /api/fortune/tts             (签文语音播报,返回 16K 单声道 PCM)
- GET  /healthz                     (存活探针)
"""
from __future__ import annotations

import asyncio

from starlette.applications import Starlette
from starlette.responses import JSONResponse, Response
from starlette.routing import Route

from ..config import get_fortune_token
from ..fortune import generate_fortune, generate_ask, generate_interpret
from ..tts import synthesize_pcm


def build_app() -> Starlette:
    async def healthz(_request):
        return JSONResponse({"status": "ok", "service": "moneygod-fortune"})

    def _fortune_auth(request):
        """返回 None 表示鉴权通过,否则返回 401 响应。"""
        token = get_fortune_token()
        if token and request.headers.get("X-Fortune-Token") != token:
            return JSONResponse({"error": "unauthorized"}, status_code=401)
        return None

    async def api_fortune_draw(request):
        denied = _fortune_auth(request)
        if denied:
            return denied
        try:
            body = await request.json()
        except Exception:  # noqa: BLE001
            return JSONResponse({"error": "invalid json"}, status_code=400)
        category = str(body.get("category") or "今日运势")
        question = str(body.get("question") or "")
        answer = str(body.get("answer") or "")
        result = await asyncio.to_thread(generate_fortune, category, question, answer)
        return JSONResponse(result)

    async def api_fortune_question(request):
        denied = _fortune_auth(request)
        if denied:
            return denied
        try:
            body = await request.json()
        except Exception:  # noqa: BLE001
            return JSONResponse({"error": "invalid json"}, status_code=400)
        lot_no = int(body.get("lot_no") or 0)
        grade = str(body.get("grade") or "中")
        poem = [str(p) for p in (body.get("poem") or [])]
        category = str(body.get("category") or "今日运势")
        result = await asyncio.to_thread(generate_ask, lot_no, grade, poem, category)
        return JSONResponse(result)

    async def api_fortune_interpret(request):
        denied = _fortune_auth(request)
        if denied:
            return denied
        try:
            body = await request.json()
        except Exception:  # noqa: BLE001
            return JSONResponse({"error": "invalid json"}, status_code=400)
        lot_no = int(body.get("lot_no") or 0)
        grade = str(body.get("grade") or "中")
        poem = [str(p) for p in (body.get("poem") or [])]
        ask = str(body.get("ask") or "")
        reply = "yes" if str(body.get("reply") or "") == "yes" else "no"
        category = str(body.get("category") or "今日运势")
        result = await asyncio.to_thread(
            generate_interpret, lot_no, grade, poem, ask, reply, category
        )
        return JSONResponse(result)

    async def api_fortune_tts(request):
        denied = _fortune_auth(request)
        if denied:
            return denied
        try:
            body = await request.json()
        except Exception:  # noqa: BLE001
            return JSONResponse({"error": "invalid json"}, status_code=400)
        text = str(body.get("text") or "").strip()
        if not text:
            return JSONResponse({"error": "text required"}, status_code=400)
        try:
            pcm = await asyncio.to_thread(synthesize_pcm, text, body.get("voice"))
        except Exception as err:  # noqa: BLE001 - 凭证缺失/上游失败统一 503,板端静默降级
            return JSONResponse({"error": f"tts unavailable: {err}"}, status_code=503)
        return Response(
            pcm,
            media_type="audio/L16",
            headers={"X-Sample-Rate": "16000", "X-Channels": "1"},
        )

    return Starlette(
        routes=[
            Route("/healthz", healthz, methods=["GET"]),
            Route("/api/fortune/draw", api_fortune_draw, methods=["POST"]),
            Route("/api/fortune/question", api_fortune_question, methods=["POST"]),
            Route("/api/fortune/interpret", api_fortune_interpret, methods=["POST"]),
            Route("/api/fortune/tts", api_fortune_tts, methods=["POST"]),
        ]
    )


__all__ = ["build_app"]
