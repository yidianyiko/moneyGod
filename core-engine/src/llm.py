"""火山引擎 Ark LLM 客户端封装。

统一走 OpenAI SDK 的 responses 接口;两个 Endpoint 共用同一 base_url 和 key。
"""
from __future__ import annotations

import json
import re
import time

from openai import OpenAI

from .config import (
    ARK_BASE_URL,
    DEEPSEEK_PRO_MODEL,
    SEED_MODEL,
    get_api_key,
)

_client: OpenAI | None = None


def client() -> OpenAI:
    global _client
    if _client is None:
        _client = OpenAI(base_url=ARK_BASE_URL, api_key=get_api_key())
    return _client


def ask(
    prompt: str,
    *,
    model: str = DEEPSEEK_PRO_MODEL,
    system: str | None = None,
    retries: int = 2,
    timeout: float | None = None,
) -> str:
    """发送文本请求,返回模型文本输出。带有限次退避重试(应对 429)。

    timeout: 单次请求超时秒数(None = SDK 默认)。
    """
    payload = prompt if system is None else f"{system}\n\n{prompt}"
    cli = client() if timeout is None else client().with_options(timeout=timeout)
    last_err: Exception | None = None
    for attempt in range(retries + 1):
        try:
            resp = cli.responses.create(model=model, input=payload)
            return resp.output_text
        except Exception as err:  # noqa: BLE001 - 顶层调用统一处理
            last_err = err
            if attempt < retries:
                time.sleep(2 ** attempt)
    raise RuntimeError(f"Ark 调用失败: {last_err}")


def ask_json(
    prompt: str,
    *,
    model: str = DEEPSEEK_PRO_MODEL,
    system: str | None = None,
    retries: int = 2,
    timeout: float | None = None,
) -> dict | list:
    """要求模型输出 JSON 并解析。"""
    text = ask(
        prompt + "\n\n严格只输出 JSON,不要任何解释文字,不要 Markdown 代码块。",
        model=model,
        system=system,
        retries=retries,
        timeout=timeout,
    )
    return _extract_json(text)


def _extract_json(text: str) -> dict | list:
    text = text.strip()
    if text.startswith("```"):
        text = re.sub(r"^```[a-zA-Z]*\n?", "", text)
        text = re.sub(r"\n?```$", "", text).strip()
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        match = re.search(r"[\{\[].*[\}\]]", text, re.S)
        if match:
            return json.loads(match.group(0))
        raise


__all__ = ["client", "ask", "ask_json", "SEED_MODEL", "DEEPSEEK_PRO_MODEL"]
