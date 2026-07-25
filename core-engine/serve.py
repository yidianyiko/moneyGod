"""启动 A2A 服务。

用法:
  .venv/bin/python serve.py                  # 默认 0.0.0.0:8000
  A2A_PUBLIC_URL=https://xxx.ngrok.app \
  HOST=0.0.0.0 PORT=8000 .venv/bin/python serve.py

对外托管(评审期间稳定在线)时,把公网地址写入 A2A_PUBLIC_URL,
使 Agent Card 中的 url 与实际可访问地址一致。
"""
import os

# 必须在任何 protobuf/grpc 相关导入之前设置:googleapis-common-protos(a2a-sdk 依赖)
# 与 pyinjective 各自 vendor 了一份 google/api/http.proto,upb(C++)后端会因重复注册报错
# TypeError: duplicate file name google/api/http.proto;纯 Python 后端能容忍这种重复注册。
os.environ.setdefault("PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION", "python")

import uvicorn

from src.a2a_server.app import build_app

app = build_app()


if __name__ == "__main__":
    host = os.environ.get("HOST", "0.0.0.0")
    port = int(os.environ.get("PORT", "8000"))
    uvicorn.run(app, host=host, port=port)
