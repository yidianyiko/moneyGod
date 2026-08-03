"""启动求签服务。

用法:
  .venv/bin/python serve.py                  # 默认 0.0.0.0:8000
  HOST=0.0.0.0 PORT=8000 .venv/bin/python serve.py
"""
import os

import uvicorn

from src.server.app import build_app

app = build_app()


if __name__ == "__main__":
    host = os.environ.get("HOST", "0.0.0.0")
    port = int(os.environ.get("PORT", "8000"))
    uvicorn.run(app, host=host, port=port)
