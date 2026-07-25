#!/usr/bin/env python3
"""Quick smoke test for VolcEngine big-model TTS (Seed TTS 2.0).

Reads credentials from env (never hard-code secrets):
    VOLC_TTS_APPID, VOLC_TTS_TOKEN, VOLC_TTS_CLUSTER (default volcano_tts)
Writes the result to /tmp/tts_<voice>.mp3 for audition.
"""
import base64
import json
import os
import uuid
import urllib.request

APPID = os.environ["VOLC_TTS_APPID"]
TOKEN = os.environ["VOLC_TTS_TOKEN"]
CLUSTER = os.environ.get("VOLC_TTS_CLUSTER", "volcano_tts")
URL = "https://openspeech.bytedance.com/api/v1/tts"

TEXT = "恭喜！第四十六签，上上签！心诚则灵，好运连连，汪汪！"

# chosen voice: 奶气萌娃 (puppy-ish child voice)
VOICES = [
    "zh_male_naiqimengwa_mars_bigtts",
]


def synth(voice):
    body = {
        "app": {"appid": APPID, "token": TOKEN, "cluster": CLUSTER},
        "user": {"uid": "cyber_fortune_test"},
        "audio": {"voice_type": voice, "encoding": "mp3", "speed_ratio": 1.0},
        "request": {"reqid": str(uuid.uuid4()), "text": TEXT, "operation": "query"},
    }
    req = urllib.request.Request(
        URL,
        data=json.dumps(body).encode("utf-8"),
        headers={
            "Content-Type": "application/json",
            "Authorization": "Bearer;" + TOKEN,
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            resp = json.loads(r.read().decode("utf-8"))
    except Exception as e:
        return f"HTTP-ERR {e}"
    code = resp.get("code")
    if code == 3000 and resp.get("data"):
        audio = base64.b64decode(resp["data"])
        path = f"/tmp/tts_{voice}.mp3"
        with open(path, "wb") as f:
            f.write(audio)
        return f"OK -> {path} ({len(audio)} bytes)"
    return f"code={code} msg={resp.get('message')}"


for v in VOICES:
    print(f"{v:45s} : {synth(v)}")
