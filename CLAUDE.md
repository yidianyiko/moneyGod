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

Everything is in this one repo:

| Path | What |
|---|---|
| `core-engine/` | Python backend — the only running service |
| `t5-dev/TuyaOpen/` | T5AI firmware — a vendored fork of `tuya/TuyaOpen` (see below) |
| `t5-dev/` | board build/flash/monitor scripts, hardware notes, font & GIF tooling |
| `stick-dev/` | StickS3 shake-remote firmware (PlatformIO) |
| `hardware/3d-models/` | enclosure STLs (13 parts) + a prebuilt zip for one-click download |
| `assets/`, `scene_change_gifs/`, `赛博财神庙_原创签谱.json` | pixel art, animations, the original lot poems |

Firmware ↔ backend coupling is the four `/api/fortune/*` HTTP routes and nothing else.

### `t5-dev/TuyaOpen/` — vendored via git subtree

Imported with `git subtree add --prefix=t5-dev/TuyaOpen ... --squash`, so this repo carries a
**single squashed snapshot**, not TuyaOpen's full history (that would have added ~200MB).
Full history lives in `github.com/yidianyiko/moneyGod-TuyaOpen`, which remains the upstream-sync
point. To pull newer TuyaOpen:

```bash
git remote add tuyaopen https://github.com/yidianyiko/moneyGod-TuyaOpen.git   # once
git subtree pull --prefix=t5-dev/TuyaOpen tuyaopen master --squash
```

Our own firmware code inside it is `apps/cyber_fortune/` and `apps/mist_core/`; everything else
is upstream Tuya SDK — don't reformat or "clean up" outside those two dirs, it makes future
subtree pulls conflict.

**Two deliberate divergences from the fork**, both forced by vendoring into a subdirectory:

1. The four former git submodules (`FlashDB`, `littlefs`, `cJSON`, `backoffAlgorithm`) are now
   **checked in as plain files**. Git only reads `.gitmodules` at the repo root, so as a subtree
   they would have become unresolvable gitlinks. `git clone` now gives you a buildable tree with
   no `git submodule update` step.
2. `tools/cli_command/util_git.py::download_submoudules()` returns early when there is no
   `.gitmodules` / the path is not a repo root. Upstream calls `Repo(repo_path)` unconditionally,
   which raises `InvalidGitRepositoryError` here and would break `tos.py build`.

### ⚠ Ticket printing is stubbed out — the real driver is missing

`fortune_printer.c` calls `bk_usbh_printer_is_connected()` / `bk_usbh_printer_write()`. **Those two
functions exist nowhere in this repo, nor in the T5AI platform** — not in any source file, not in any
prebuilt `.a` (verified with `nm` across the whole platform tree). CherryUSB does ship
`class/printer/usbh_printer.c`, but it is not listed in `components/bk_usb/CMakeLists.txt` (so it is
never compiled) and it defines no `bk_usbh_printer_` wrapper.

The working implementation only ever lived in one developer's local `platform/T5AI/` checkout.
TuyaOpen's own `.gitignore` excludes that directory and `tos.py` re-clones it from Gitee on demand,
so the code was never committed anywhere and a clean checkout failed at the final link.

`src/fortune_printer_stub.c` restores a buildable tree by defining both symbols as no-ops. Screen,
TTS, BLE shake remote and networked draw all work; **the thermal ticket does not print** — and the
customization-WeChat QR only exists on that ticket, so this is a real feature gap, not cosmetic.

To restore printing: land the real implementation under `apps/cyber_fortune/` (tracked territory —
do **not** put it back in `platform/`, it will be wiped) and define `FORTUNE_HAVE_USB_PRINTER`, which
compiles the stub away. The stub deliberately avoids `__attribute__((weak))`: a weak definition in
`libtuyaapp.a` would satisfy the reference during the archive scan and stop the linker from pulling
the strong definition out of a platform library, leaving printing silently dead.

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

Firmware (from the repo root; the scripts resolve `t5-dev/TuyaOpen/` relative to themselves, so
they work from any checkout location):

```bash
bash t5-dev/cf_build.sh          # incremental build
bash t5-dev/cf_build.sh clean    # full clean first (needed after a BOARD/config change)
bash t5-dev/cf_flash.sh  [port] [baud]   # default 921600
bash t5-dev/cf_monitor.sh [port] [baud]  # log is on the *503* CDC port @ 460800
```

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
