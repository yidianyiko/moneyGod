# PandaAI 后端开发备忘(信息汇总)

> 汇总后续开发反复要用的信息:接入凭证、资源链接、提交要求、已实现状态、功能清单。
> 敏感 Token 只存放于 `core-engine/.env`(已 gitignore),本文档不写明文 Token。

---

## 1. 火山引擎 Ark(LLM 接入)

| 项 | 值 |
|---|---|
| base_url | `https://ark.cn-beijing.volces.com/api/v3` |
| 鉴权环境变量 | `ARK_API_KEY`(存于 `core-engine/.env`) |
| Token 数量 | 4 个,任选其一;遇 429/限流切换另一个 |
| SDK | `openai`(Python),用 `client.responses.create(model=..., input=...)` |
| 取文本 | `response.output_text` |

**两个 Endpoint(是接入点 ID,不是 Token,填在 `model` 字段):**

| 模型 | Endpoint ID | 用途 |
|---|---|---|
| 火山低延时 Seed | `ep-20260722093003-7swj9` | 快速交互、多模态(支持图片) |
| DeepSeek-Pro | `ep-20260708162855-pcf9x` | 复杂推理、分析 |

**安全要求(务必遵守):**
- Token 仅限本次 AdventureX 使用,不得外传/公开。
- 不写进源码、Git、截图、日志;`.env` 已加入 `.gitignore`。
- 加异常处理 + 请求超时 + 有限退避重试;429 不要无限重试。
- 前端不得直连模型,必须经自己的后端。
- 图片 URL 须公网 HTTPS 可直接访问(不依赖登录/Cookie)。
- 缓存可复用结果,减少共享额度浪费。

**多模态调用**:Seed 模型可在一个请求内同时传 `input_image`(image_url)+ `input_text`。

---

## 2. PandaAI 平台资源

| 资源 | 地址 / 获取方式 |
|---|---|
| 数据服务领取 | https://www.pandaaiquant.com/data-service (注册官网 + 加飞书群,得 7 天全量数据权限) |
| 数据 API 文档 | https://www.pandaaiquant.com/data-service/api-docs?api=data_overview |
| 数据覆盖 | 行情、财务、指数、行业、宏观、因子、交易日历 |
| QuantSkills(金融 Skills) | https://github.com/quantskills (数据查询/因子分析/策略回测/绩效风险/可视化/报告生成) |
| DeepSeek API 细节 | 比赛期间飞书群发放(本项目已改用火山 Ark 的 DeepSeek-Pro) |
| 示例 Agent(Agent Card/调用/消息格式) | 飞书群内提供网址 |
| Agent 测试环境 | 飞书群内提供,提交前联调用 |
| 技术支持 | PandaAI 展位 / 飞书群 |

> ⏳ 待办:WebFetch 曾被限流,数据 API 文档与 QuantSkills 的具体接入方式尚未确认;需注册领权限后再看。

---

## 3. PandaAI 赛道提交硬要求(18)

- 以 **A2A Remote Agent** 形式提交,自行托管服务。
- 提交 **可公开访问的 Agent Card URL**,如 `https://example.com/.well-known/agent-card.json`。
- 支持 **A2A 协议** 调用;Agent Card 信息完整、真实、可访问。
- 服务在评审期间 **稳定在线**。
- 底座模型 **统一 DeepSeek(V4 pro)**——本项目用火山 Ark 的 DeepSeek-Pro Endpoint。
- Agent 总响应时间 **≤ 20 分钟**(代码常量 `MAX_RESPONSE_SECONDS`)。
- 能处理平台发来的 **自然语言任务**;过程与结果 **清晰可解释**。
- **不得绕过权限访问未授权数据**。
- 输出 **必须含风险提示**。
- 完成 **≥ 3 个示例任务** 测试;提交说明文档(场景/架构/Skills 调用/结果展示)+ 演示视频。
- GitHub 仓库可访问,或邮件发 code@pandaai.online。

---

## 4. 当前已实现(core-engine / M2)

```
core-engine/
├── .env(Token) / .env.example / .gitignore
├── requirements.txt + .venv/   # openai 2.47.0, python-dotenv
├── quickstart.py               # ✅ Ark 连通性已验证
├── main.py                     # ✅ 跑通一次 daily_draw
└── src/
    ├── config.py        # base_url + 双 Endpoint + 20min 约束 + get_api_key()
    ├── llm.py           # ask()/ask_json();退避重试;JSON 抽取
    ├── schemas.py       # UserProfile / Holding / Signal / QuantConclusion
    ├── data_adapter.py  # M3 mock(A股+基金样本),含 dataSources 溯源
    ├── engine.py        # daily_draw 编排
    └── agents/          # planner(Seed) / research / strategy(DeepSeek-Pro)
```

**跑通验证**:三 Agent 真实调用,对保守型用户产出克制的 hold 建议,无违规措辞。

**运行方式:**
```bash
cd core-engine
.venv/bin/python quickstart.py   # 验证 Ark
.venv/bin/python main.py         # 跑一次每日抽签
```

---

## 5. 要实现的功能清单

### 5.1 核心引擎 M2(部分已完成)
- [x] `daily_draw(profile, holdings) -> QuantConclusion`:每日抽签的量化分析
- [x] 多 Agent:Planner / Research / Strategy
- [ ] **A2A 通用任务入口** `handle_task(task)`:处理平台的自由自然语言任务(投研/策略问答),而不只是 daily_draw 场景
- [ ] Report/归因输出的可解释性增强(过程可读)
- [ ] 结果缓存(同输入不重复调用,省额度)

### 5.2 A2A Gateway M1(未开始,提交刚需)
- [ ] 暴露 `GET /.well-known/agent-card.json`(Agent Card)
- [ ] A2A 协议任务端点(接收/回复平台任务)
- [ ] 鉴权 + 超时控制(≤20min)
- [ ] 给前端用的 REST 入口 `POST /api/daily-draw`
- [ ] 自托管上线(评审期间稳定在线)

### 5.3 数据适配层 M3(mock 已就位)
- [ ] 接入真实 PandaAI 数据 API / QuantSkills(替换 `data_adapter.py`)
- [ ] 覆盖:行情 / 财务 / 因子 / 交易日历(MVP:A股+基金)
- [ ] (可选)接入 QuantSkills 的策略回测能力

### 5.4 合规(贯穿输出层)
- [ ] 每条建议带风险提示
- [ ] 免责声明:"非玄学,基于 XX 数据,投资有风险"
- [ ] AI 边界声明:不承诺收益、不代客理财
- [ ] 禁用措辞拦截("稳赚/必涨/保本高收益")
- [ ] 异常/数据缺失场景走保守中性提示

> 注:合规的用户可读表达主要在 M4 投顾表达层完成,但 core-engine 输出的 QuantConclusion 已通过 `dataSources` 支持溯源、prompt 中已约束不使用违规措辞。

### 5.5 提交物
- [ ] ≥3 个示例任务及预期输出
- [ ] 说明文档(场景/架构/Skills 调用/结果展示)
- [ ] 演示视频(完整核心流程)
- [ ] GitHub 仓库 / 或邮件材料

---

## 6. 下一步决策点
- 先接真实数据(M3),还是先做 A2A Gateway(M1)?
- 数据权限是否已领取(注册 PandaAI 官网 + 加飞书群)?
