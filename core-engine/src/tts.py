"""火山豆包语音合成(Seed TTS 2.0)封装 —— 文本转 16K 单声道 PCM。

供 /api/fortune/tts 路由调用。板端 tdl_audio 只吃 16K/16bit/mono PCM,
后端直接返回裸 PCM 字节流,板端零解码。
"""
from __future__ import annotations

import base64
import json
import logging
import urllib.request
import uuid

from .config import VOLC_TTS_URL, get_tts_config

logger = logging.getLogger(__name__)

TTS_TIMEOUT_SECONDS = 15.0


def synthesize_pcm(text: str, voice: str | None = None) -> bytes:
    """合成 16K 单声道 16bit PCM;失败抛异常,由路由层转为错误响应。"""
    cfg = get_tts_config()
    body = {
        "app": {"appid": cfg["appid"], "token": cfg["token"], "cluster": cfg["cluster"]},
        "user": {"uid": "cyber_fortune_t5"},
        "audio": {
            "voice_type": voice or cfg["voice"],
            "encoding": "pcm",
            "rate": 16000,
            "speed_ratio": 1.0,
        },
        "request": {"reqid": str(uuid.uuid4()), "text": text, "operation": "query"},
    }
    req = urllib.request.Request(
        VOLC_TTS_URL,
        data=json.dumps(body).encode("utf-8"),
        headers={
            "Content-Type": "application/json",
            "Authorization": "Bearer;" + cfg["token"],
        },
    )
    with urllib.request.urlopen(req, timeout=TTS_TIMEOUT_SECONDS) as r:
        resp = json.loads(r.read().decode("utf-8"))
    if resp.get("code") != 3000 or not resp.get("data"):
        raise RuntimeError(f"TTS 失败 code={resp.get('code')} msg={resp.get('message')}")
    return base64.b64decode(resp["data"])


__all__ = ["synthesize_pcm"]
