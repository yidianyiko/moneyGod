# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

MoneyGod (财神) / 赛博财神庙 — an AdventureX 2026 hackathon project. Concept: a desktop "money god"
figurine (Tuya T5AI board + thermal printer + shake-to-draw remote) that gives a "抽签" (fortune-draw)
ritual: shake → draw a lot → LLM-generated poem + interpretation → TTS playback → printed ticket.

The project scope was deliberately narrowed to **the board + the backend API it calls**. The
following were cut and must not be reintroduced without an explicit decision:

- the **A2A / PandaAI "Next AI Trader"** track (Agent Card, JSON-RPC endpoint, `a2a-sdk`),
- the **Injective on-chain notary** (`notary.py`, `injective-py`),
- the **multi-agent quantitative research engine** (`workflow.py`, `tasks.py`, `engine.py`,
  `quant/`, `agents/`, `data_adapter.py`, `panda_data`),
- the **web UI** (`/`, `/api/analyze`, the static demo page and its GIF assets),
- the **Spectrum iMessage bridge** (`spectrum/`, root Node project).

Historical planning docs at the repo root (`需求文档.md`, `模块拆分.md`) still describe the old
M1–M5 monorepo split and the two-track ambition — **they are stale**; treat this file as the truth.

## Repo layout

| Path | What |
|---|---|
| `core-engine/` | Python backend — the only running service |
| `t5-dev/` | T5AI board build/flash/monitor scripts, hardware notes, font & GIF tooling |
| `stick-dev/` | StickS3 shake-remote firmware (PlatformIO) |
| `assets/`, `scene_change_gifs/`, `赛博财神庙_原创签谱.json` | pixel art, animations, the original lot poems |

The T5AI firmware lives in a **separate repo**: `github.com/yidianyiko/moneyGod-TuyaOpen`
(a fork of `tuya/TuyaOpen`; your code is `apps/cyber_fortune/` + `apps/mist_core/`). It is
intentionally not a submodule — `.gitignore` expects it cloned at `t5-dev/TuyaOpen/`.
It talks to this backend over the four `/api/fortune/*` routes below; that HTTP contract is the
only coupling between the two repos.

## Commands

All commands run from `core-engine/` using the project's venv (already provisioned):

```bash
cd core-engine
.venv/bin/python quickstart.py            # verify Volcano Ark LLM connectivity
.venv/bin/python serve.py                 # start the fortune API (default 0.0.0.0:8000)
HOST=0.0.0.0 PORT=8000 .venv/bin/python serve.py
.venv/bin/python -m pytest tests -q       # test suite (7 tests, all deterministic — no LLM calls)
```

Install/update deps: `.venv/bin/pip install -r requirements.txt` (`requirements-dev.txt` adds pytest).

### Required env (`core-engine/.env`, gitignored — copy from `.env.example`)

- `ARK_API_KEY` — Volcano Engine Ark token (4 available, rotate on 429).
- `VOLC_TTS_APPID` / `VOLC_TTS_TOKEN` — 豆包语音 Seed TTS 2.0, a **separate** credential pair from
  `ARK_API_KEY`; missing values make `/api/fortune/tts` return 503 and the board falls back silently.
- `FORTUNE_API_TOKEN` — optional; when set, every `/api/fortune/*` call must send it as
  `X-Fortune-Token`. Empty means no auth (dev).

Never put these tokens in code, commits, logs, or screenshots.

## Architecture

The backend is deliberately small — five modules under `core-engine/src/`.

### LLM layer (`src/config.py`, `src/llm.py`)

Everything goes through Volcano Engine Ark via the `openai` SDK's `responses.create`, using two
endpoint IDs (not API keys) selected per task:

- `SEED_MODEL` — fast, low-latency, multimodal.
- `DEEPSEEK_PRO_MODEL` — heavier reasoning.

`llm.ask()` / `llm.ask_json()` wrap this with bounded exponential-backoff retry and tolerant JSON
extraction (strips ```` ```json ```` fences, falls back to regex-extracting the first `{...}`/`[...]`).

### Fortune generation (`src/fortune.py`)

The product core. `draw_grade()` picks a grade/score from a weighted random draw; `generate_fortune()`
builds a prompt from category + the user's question + their yes/no answer and asks the LLM for the
poem, explanation and advice. `generate_ask()` produces the follow-up yes/no question, and
`generate_interpret()` the targeted reading once the user answers.

Two invariants worth preserving:

- **Every path returns a well-formed result.** LLM failures fall through to `_fallback_result()`,
  which serves a canned lot, so the board always gets a 200 and never has to handle an error state.
- **`_sanitize_text()` strips anything not encodable in GBK.** The board's font and the EM5820H
  thermal printer are GBK-only; unencodable characters show as garbage or drop glyphs. Any new
  user-facing string must go through it.

### TTS (`src/tts.py`)

Wraps 豆包语音 Seed TTS 2.0. Returns raw **16kHz / 16-bit / mono PCM** — that is exactly what the
board's `tdl_audio` accepts; don't change the format without changing the firmware.

### HTTP server (`src/server/app.py`)

Plain Starlette (no A2A SDK). `build_app()` exposes:

- `POST /api/fortune/draw` — 抽档 + 生成签文
- `POST /api/fortune/question` — 结合签文生成解签是非题
- `POST /api/fortune/interpret` — 结合是非回答生成针对性解签
- `POST /api/fortune/tts` — 签文语音播报, returns 16K mono PCM
- `GET /healthz` — liveness probe

All four fortune routes share `_fortune_auth()` and call into `fortune.py`/`tts.py` via
`asyncio.to_thread` — those do blocking network/LLM I/O and must never run on the event loop.

### Removed: the compliance guardrail

`src/compliance.py` (promise-of-return phrase scrubbing + `RISK_DISCLAIMER` appending) was deleted
along with the quant/A2A paths it used to guard — `fortune.py` was never wired to it. The generated
`advice` field does talk about money in a light, behavioural way ("把已攒的钱数核对一遍"), but it no
longer gives securities advice, so there is no disclaimer machinery in the codebase at all. If the
prompt in `fortune.py` ever drifts back toward concrete investment recommendations, that guardrail
needs to come back — see git history for the original module.
