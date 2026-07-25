# 赛博财神庙 · 声音功能设计（音效 + TTS 播报）

- 日期：2026-07-25
- 状态：设计定稿（待实施）
- 关联硬件：T5AI-Board + JST PH 1.25 2P 外接无源喇叭（GPIO28 PA 使能，已验证出声）

## 1. 目标

在现有 5 场景交互流程（待机→分类→问题→摇签→结果）上叠加两类声音，
增强仪式感与"赛博财神庙"氛围，且必须**全程可降级**：喇叭未接 / codec 失败 /
断网时，主流程（求签、显示、打印）零影响。

## 2. 两类声音

### 2.1 板端 8-bit 合成音效（离线，无素材文件）

代码实时合成正弦/噪声波形，编译进固件，走 `tdl_audio_play` 16K 单声道播放。
沿用像素 UI 风格，断网照常。

| 时机 | 场景钩子 | 音效 |
|---|---|---|
| 触发求签（轻触/摇 Stick） | 进入 SCENE_DRAW | 短促"叮" |
| 摇签动画进行中 | getting_lottery GIF | 签筒"沙沙"（噪声脉冲）|
| 出签瞬间 | lottery_get | 金币三音阶（C6-E6-G6，已实测）|
| 送打印 | 打印按钮回调 | 轻"滴" |

### 2.2 TTS 奶气萌娃播报（结果页自动播）

- **触发**：进入 SCENE_RESULT 且拿到签文后，异步请求，**不阻塞画面**。
- **文案**：`恭喜！第{N}签，{吉凶档位}！{四句签诗}……汪汪！`（约 15 秒）
- **声音**：火山语音合成大模型，voice_type = `zh_male_naiqimengwa_mars_bigtts`（奶气萌娃），结尾由 TTS 直接念"汪汪"。

## 3. 后端接口 `/api/fortune/tts`

- **入参**：`{ "text": "..." , "voice"?: "...", "encoding"?: "mp3|pcm" }`
  - `voice` 缺省取 `VOLC_TTS_VOICE`；`encoding` 缺省 `mp3`。
- **实现**：调火山 `https://openspeech.bytedance.com/api/v1/tts`
  - header `Authorization: Bearer;{token}`
  - body：`app{appid,token,cluster}` + `audio{voice_type,encoding,speed_ratio}` + `request{reqid,text,operation:"query"}`
  - 成功码 `code==3000`，`data` 为 base64 音频。
- **凭证**（服务器 `.env`，与 `ARK_API_KEY` 并列，不入代码）：
  `VOLC_TTS_APPID` / `VOLC_TTS_TOKEN` / `VOLC_TTS_CLUSTER=volcano_tts` / `VOLC_TTS_VOICE=zh_male_naiqimengwa_mars_bigtts`
- **返回给板端**：直接回传音频字节流（`Content-Type: audio/mpeg` 或裸 PCM），
  板端边收边解码播放。（MP3 需板端解码器；若板端解码不便，后端可直接返回 16K 单声道 PCM，见 §5 决策）

## 4. 板端播放链路

- 复用已建的 `fortune_audio` 模块（`tdl_audio` 句柄）。
- 结果页出现后，起一个后台任务：POST `/api/fortune/tts` → 收到音频 → 分块喂 `tdl_audio_play`。
- 失败（HTTP 错误/超时/codec 未就绪）→ 静默跳过，仅打日志。
- 音量默认 80%（`tdl_audio_volume_set`），现场可调。

## 5. 待定的实现级决策（实施时确认，不影响本设计）

1. **音频格式**：MP3（省带宽，需板端 MP3 解码，SDK 有 `ai_player`）vs 16K PCM（板端零解码，直接播，带宽大）。倾向 **PCM**：结果页文案短、局域网、实现最简。
2. **签诗是否逐字念全**：已定"签号+签诗"，约 15 秒。

## 6. 验收

- 后端：`curl /api/fortune/tts` 返回可播放音频；缺凭证时优雅报错。
- 板端：结果页出现 → 数秒内奶气萌娃念出签号+签诗+"汪汪"；断网/拔喇叭 → 无异常。
- 音效：四个时机各响一次，与动画同步。

## 7. 改动范围

- 后端：`core-engine/src` 新增 `/api/fortune/tts`（约 60 行）+ 配置读取。
- 板端：`fortune_audio.c/.h` 扩展（音效合成 + TTS 流播放，约 200 行）；各 scene 插播放调用。
- 容错：全部静默降级，主流程零影响。
