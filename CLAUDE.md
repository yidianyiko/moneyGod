# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

MoneyGod (财神) — an AdventureX 2026 hackathon project. Concept: a desktop "money god" figurine that
gives a daily "抽签" (fortune-draw) ritual backed by a real multi-agent quantitative research engine —
"玄学皮 · 量化芯" (mystic skin, quant core). It targets two hackathon tracks simultaneously:

- **度小满 Money Whisperer**: consumer-facing AI investment advisor, plain-language advice + mandatory
  compliance/risk disclaimers, aimed at broke-by-month-end young professionals.
- **PandaAI Next AI Trader**: a self-hosted **A2A Remote Agent** with a publicly reachable Agent Card,
  DeepSeek-based, must respond within 20 minutes, must show explainable multi-agent collaboration.

All actual implemented code lives under `core-engine/` (Python). The repo root only holds Chinese
planning docs (`需求文档.md` requirements, `模块拆分.md` module split, `PandaAI开发备忘.md` dev notes
with credentials/endpoints info) and an unused stub `package.json`/`node_modules` (no `index.js`,
not part of the running system — ignore unless asked to build it out).

The planning docs describe a larger monorepo split (M1 Gateway / M2 Engine / M3 Data / M4 Advisor
expression / M5 Web frontend), but only **M2 (core engine) + M1 (A2A gateway) + M3 (data adapter)**
are implemented so far, all inside `core-engine/`.

## Commands

All commands run from `core-engine/` using the project's venv (already provisioned):

```bash
cd core-engine
.venv/bin/python quickstart.py   # verify Volcano Ark LLM connectivity
.venv/bin/python main.py         # run one daily_draw() pass end-to-end, prints QuantConclusion JSON
.venv/bin/python serve.py        # start the A2A server (default 0.0.0.0:8000)
```

Serving with a public URL (so the Agent Card's `url` matches how it's actually reached):

```bash
A2A_PUBLIC_URL=https://xxx.ngrok.app HOST=0.0.0.0 PORT=8000 .venv/bin/python serve.py
```

Install/update deps: `.venv/bin/pip install -r requirements.txt`.

There is no test suite yet.

### Required env (`core-engine/.env`, gitignored — copy from `.env.example`)

- `ARK_API_KEY` — Volcano Engine Ark token (4 available, rotate on 429).
- `PANDA_USERNAME` / `PANDA_PASSWORD` — PandaAI `panda_data` SDK auth (`86` + phone number as username).

Never put these tokens in code, commits, logs, or screenshots.

## Architecture

### LLM layer (`src/config.py`, `src/llm.py`)

Everything goes through Volcano Engine Ark via the `openai` SDK's `responses.create`, using two
endpoint IDs (not API keys) selected per task:

- `SEED_MODEL` — fast, low-latency, multimodal (used by Planner for lightweight decomposition).
- `DEEPSEEK_PRO_MODEL` — heavier reasoning (used by Research/Strategy/Report — the PandaAI-mandated
  DeepSeek backbone).

`llm.ask()` / `llm.ask_json()` wrap this with bounded exponential-backoff retry and tolerant JSON
extraction (strips ```` ```json ```` fences, falls back to regex-extracting the first `{...}`/`[...]`).
`config.MAX_RESPONSE_SECONDS` (1200s) encodes PandaAI's ≤20-minute response constraint.

### The one non-negotiable design rule: numbers come from code, never from the LLM

Every quant module's docstring repeats this: **all performance/statistical numbers (returns, Sharpe,
drawdown, factor scores, volatility...) are computed deterministically in Python** (`src/quant/*`,
`src/data_adapter.py`). LLMs (`src/agents/*`) only do planning, interpretation, and report prose —
they are explicitly instructed never to invent or restate a number that didn't come from upstream
deterministic output. This is both a PandaAI/度小满 compliance requirement (auditable, non-misleading
output) and the project's judging differentiator ("all figures are reproducible").

### Compliance guardrail (`src/compliance.py`)

Pure deterministic module, no LLM calls, sits in front of every externally-facing response:

- `scrub()` — replaces a hardcoded blacklist of promise-of-return phrases (稳赚/必涨/保本/翻倍/...)
  with a neutral placeholder.
- `ensure_disclaimer()` — scrubs, then appends `RISK_DISCLAIMER` (non-mystic / risk / AI-boundary /
  not-a-guarantee statement) unless one is already present.

Any new code path that produces user-facing or A2A-facing text **must** be run through
`compliance.ensure_disclaimer()` before returning.

### Data layer (`src/data_adapter.py`, `src/quant/factors.py`)

Wraps the `panda_data` SDK (lazily `init_token()`'d from env on first use). Scope is MVP-only:
A-share stocks (`get_market_data`) + on-exchange ETF/LOF funds (`get_fund_daily` — off-exchange OF
funds aren't covered). `panda_data.get_factor()` supplies price/volume/fundamental panels;
`get_fina_performance()` supplies ROE/YoY growth. There is **no backtest Skill on the platform** —
`src/quant/backtest.py` is a from-scratch, no-lookahead rolling walk-forward backtester, which the
team treats as a compliance/differentiation feature ("we compute it ourselves, so it's reproducible").
All DataFrames from `panda_data` come back date-descending; every call site re-sorts ascending before
computing metrics — keep that pattern when touching this code.

### Two orchestration entry points, both converging on the same primitives

1. **`src/engine.py` → `daily_draw(profile, holdings)`** — the original, simpler "抽签" pipeline used
   by `main.py`: `planner.plan()` → `data_adapter` (market overview + holdings snapshot) →
   `research.analyze()` → `strategy.decide()` → assembled into a `QuantConclusion`
   (see `src/schemas.py` for the shared dataclass contracts: `UserProfile`, `Holding`, `Signal`,
   `QuantConclusion`).

2. **`src/workflow.py` → `research_workflow(query, risk_level)`** — the richer "冲奖版" (award-chasing)
   pipeline with a genuine feedback loop, used by the A2A task path. Chain:
   `Planner (extract_targets)` → `Factor (quant/factors.score_universe)` →
   `Strategy (agents/strategy_quant.propose)` → `Backtest (quant/backtest.run_backtest)` →
   **if Sharpe < risk-adjusted target (`SHARPE_TARGET`/`_risk_to_target`), loop back to
   `strategy_quant.revise()` with the real backtest numbers, up to `MAX_ITERS=3`** → `Risk
   (quant/risk.portfolio_risk)` → `Report (agents/report.write)`. Every step appends to a `trace`
   list that's returned alongside the result — this trace is what gets rendered as the visible
   "multi-agent collaboration" in both the A2A text response and the web UI, and is the thing judges
   are shown as evidence of "real" (not just sequential) multi-agent collaboration.

`src/tasks.py` is the single entry point that decides which pipeline a free-text task should use:
`extract_targets()` regex-parses A-share/fund codes out of natural language (6-digit code, optional
`.SH`/`.SZ`/`.OF` suffix, guesses exchange/asset-type when suffix is absent); `_wants_workflow()`
routes to `research_workflow` if codes were found or workflow-intent keywords appear (策略/回测/因子/
组合/alpha/sharpe/...), otherwise falls through to a plain market-data-grounded QA answer via the LLM.
`handle_task()` returns rendered text (for A2A); `analyze_structured()` returns the raw dict (for the
web UI's `/api/analyze`). Both funnel every output through `compliance.ensure_disclaimer()`/`scrub()`
and have a top-level `except` that degrades to a conservative canned response rather than ever
returning an unhandled error — preserve that "always converge to a response" behavior in this file.

### A2A server (`src/a2a_server/`)

Built on `a2a-sdk` + Starlette. `app.py` assembles the app and exposes:

- `GET /.well-known/agent-card.json` — the PandaAI-mandated discovery path.
- `GET /.well-known/agent.json` — SDK-default path, kept for compatibility.
- `POST /` — A2A JSON-RPC endpoint (`message/send`, `tasks/get`, etc.), handled by
  `executor.MoneyGodAgentExecutor` which bridges to `tasks.handle_task()` via `asyncio.to_thread`
  (that call does blocking network/LLM I/O — never call it directly on the event loop).
- `POST /api/analyze` — REST endpoint for the web UI, calls `tasks.analyze_structured()`.
- `GET /` — serves the static demo page (`src/a2a_server/web/index.html`).
- `GET /healthz` — liveness probe.

`card.py` defines the `AgentCard` (name, description, skills/examples shown to PandaAI's platform) —
update `_SKILLS` here when adding a new capability, since this is what graders/the platform see before
ever calling the agent. `PUBLIC_BASE_URL` comes from `A2A_PUBLIC_URL` env — must match wherever the
service is actually reachable during judging.
