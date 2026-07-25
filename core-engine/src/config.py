"""火山引擎 Ark 接入配置。"""
import os
from dotenv import load_dotenv

load_dotenv()

ARK_BASE_URL = "https://ark.cn-beijing.volces.com/api/v3"

# 两个推理接入点(Endpoint ID,不是 Token)
SEED_MODEL = "ep-20260722093003-7swj9"        # 火山低延时 Seed 模型,支持多模态
DEEPSEEK_PRO_MODEL = "ep-20260708162855-pcf9x"  # DeepSeek-Pro,复杂推理

# PandaAI 约束:A2A 总响应时间 ≤ 20 分钟
MAX_RESPONSE_SECONDS = 20 * 60


def get_api_key() -> str:
    key = os.getenv("ARK_API_KEY")
    if not key:
        raise RuntimeError(
            "未找到 ARK_API_KEY。请复制 .env.example 为 .env 并填入火山引擎 Token。"
        )
    return key.strip()


def get_fortune_token() -> str:
    """求签接口鉴权 token;空字符串表示不校验(开发环境)。"""
    return (os.getenv("FORTUNE_API_TOKEN") or "").strip()


# 火山豆包语音合成大模型(Seed TTS 2.0),与 ARK 是两套独立凭证
VOLC_TTS_URL = "https://openspeech.bytedance.com/api/v1/tts"


def get_tts_config() -> dict:
    """TTS 凭证与音色;appid/token 缺失时抛 RuntimeError,由路由层降级。"""
    appid = (os.getenv("VOLC_TTS_APPID") or "").strip()
    token = (os.getenv("VOLC_TTS_TOKEN") or "").strip()
    if not appid or not token:
        raise RuntimeError("未配置 VOLC_TTS_APPID / VOLC_TTS_TOKEN,TTS 不可用")
    return {
        "appid": appid,
        "token": token,
        "cluster": (os.getenv("VOLC_TTS_CLUSTER") or "volcano_tts").strip(),
        "voice": (os.getenv("VOLC_TTS_VOICE") or "zh_male_naiqimengwa_mars_bigtts").strip(),
    }
