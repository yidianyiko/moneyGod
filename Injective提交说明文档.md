# 财神 MoneyGod × Injective「AI x Blockchain 创新赛道」提交说明文档

> 玄学皮 · 量化芯 · 链上魂。让 AI 说的每一句"签文",都有一笔任何人都能独立核验的链上凭证。

---

## 1. 项目名称与简介(Pitch)

**财神 MoneyGod** —— 一支多 Agent 量化投研团队,把"因子研究 → 策略生成 → 回测验证 → 风险评估
→ 报告撰写"的完整投研流程,做成可被 A2A 协议调用的 Remote Agent(同时是 PandaAI「Next AI
Trader」与度小满「Money Whisperer」两条赛道的提交项目)。

本次 Injective 赛道提交的是在此基础上新增的**链上签文存证**能力:每次投研工作流跑完后,系统
把"签文 + 大白话建议 + 投研报告 + 免责声明 + 关键回测指标"做哈希,广播一笔携带 memo 的交易到
Injective 测试网,把这份 AI 决策**永久、不可篡改地**记录在公链上。任何人拿到 API 返回的 JSON,
都能自己重新计算出同一个哈希,去链上比对 —— 这不是"我们说这是真的",而是"你可以自己验证"。

**为什么是 Injective**:Injective 亚秒级出块 + 接近零 Gas 费,使得"每次分析都上链存证"这种高
频、低价值单笔交易的场景在经济和体验上都可行 —— 在大多数其他公链上,这个设计会因为出块慢或
Gas 太贵而不现实。

---

## 2. 可运行的 Demo

| 项 | 内容 |
|---|---|
| 在线体验(Web UI) | http://47.98.99.199:8000/ |
| REST 调试接口 | `POST http://47.98.99.199:8000/api/analyze`,body:`{"text": "...", "riskLevel": "conservative\|balanced\|aggressive"}` |
| A2A Agent Card | http://47.98.99.199:8000/.well-known/agent-card.json |
| 健康检查 | http://47.98.99.199:8000/healthz |

Web UI 里提交任意投研请求后,结果页面除了签文、多 Agent 协作轨迹、因子/回测/风险数据外,还会
新增一块「⛓️ 链上存证」模块,展示真实交易哈希与区块浏览器链接。

---

## 3. 开源代码仓库

https://github.com/yidianyiko/moneyGod (公开)

本次 Injective 集成的核心代码:
- `core-engine/src/notary.py` —— 哈希规范化 + Injective 测试网广播
- `core-engine/src/advisor.py` —— 签文/建议生成(纯规则,不调用 LLM)
- `core-engine/src/workflow.py` —— 把 Advisor/Notary 接入多 Agent 工作流末尾
- `core-engine/scripts/generate_injective_wallet.py` —— 测试网密钥对生成
- `core-engine/tests/test_notary.py` —— 哈希逻辑单元测试

---

## 4. 架构图 / 技术方案说明

### 4.1 在现有多 Agent 工作流末尾新增两环

```
用户任务(自然语言)
  │
  ▼
Planner → Factor → Strategy ⇄ Backtest(反馈闭环,最多 3 轮)→ Risk → Report
  │
  ▼
Advisor Agent   产出签文 + 大白话建议 + action 判定(宜进/宜守/宜减/宜观)
  纯规则,由真实回测夏普/最大回撤/风险等级决定,不调用 LLM,避免生成式文本引入违规措辞
  │
  ▼
Notary Agent    对 {签文, 建议, 报告, 免责声明, 关键回测指标, 时间戳} 做规范化 JSON + sha256
  → 用官方 pyinjective SDK 构建/签名/广播一笔 MsgSend(自转账)到 Injective 测试网
  → memo = "moneygod:v1:<hash>"
  → 链上广播失败绝不阻断主流程,一律 catch 后返回 {ok:false, reason}
  │
  ▼
返回:投研结论 + advice + onchain{txHash, explorerUrl, hash} + 完整协作轨迹(trace)
```

### 4.2 技术选型

| 决策 | 说明 |
|---|---|
| 上链方式 | `MsgSend`(自转账最小单位)+ memo 携带哈希,不写自定义合约。黑客松时间约束下,这是"真实广播一笔可验证测试网交易"且风险最低的路径 |
| SDK | 官方 `pyinjective`(PyPI 包名 `injective-py`),与现有 core-engine 纯 Python 技术栈一致 |
| 开发工具链 | 用官方 `@injectivelabs/ainj` 工具接入 Injective 官方 Claude Code Agent Skills(`injective-wallet-ops`/`injective-cli`/`injective-faucet` 等)与 MCP 文档检索能力,核对钱包生成方式、地址编码、测试网端点/gas 参数,避免凭记忆猜官方 API |
| 网络 | Injective 测试网,`chain_id=injective-888`,`fee_denom=inj` |
| 容错设计 | 广播 10 秒超时;`SimulatedTransactionFeeCalculator` 用 1.4x gas 缓冲(开发中发现默认 1.3x buffer 在真实测试网上会因估算偏差导致 out-of-gas 执行失败,已修复并验证);长期运行的 gRPC 连接会显式关闭,避免生产环境连接泄漏 |

### 4.3 可验证性 —— 核心设计原则

哈希对象是**确定性、key 排序**的 JSON(`_canonical_payload` 精确抽取 6 个字段,`json.dumps(...,
sort_keys=True, ensure_ascii=False, separators=(",", ":"))`)。这意味着:

1. 任何人拿到 `/api/analyze` 返回的 JSON,都能用同样的字段抽取 + 排序 + sha256,独立算出同一个哈希
2. 拿这个哈希去比对链上交易的 memo 字段(公开可查,无需任何权限)
3. 两者一致,就证明这条 AI 决策自广播那一刻起从未被篡改

下面的示例任务附带了这个完整验证链路的真实运行结果,不是编造的。

---

## 5. 数据 / 开发工具 使用清单

| 能力 | 来源 | 用法 |
|---|---|---|
| Injective 官方 Python SDK(`pyinjective` / PyPI 包 `injective-py`) | 官方 | 钱包生成、交易构建/签名/广播、余额查询、gas 模拟 |
| `@injectivelabs/ainj` 官方 AI 开发工具 | 官方 | 打包了 `injectived` CLI + Injective 官方 MCP servers(链上工具 + 官方文档检索) + 官方 Claude Code Agent Skills,用于开发阶段核对 API 用法与测试网参数 |
| Injective Agent Skills(`injective-wallet-ops`/`injective-cli`/`injective-faucet` 等) | 官方(`InjectiveLabs/agent-skills`) | 钱包生成/地址编码规范、CLI 命令参考、faucet 领水流程指引 |
| Injective 测试网水龙头 | 官方(`https://testnet.faucet.injective.network/`) | 为专用测试网钱包领取 gas 费 |
| Injective 测试网 LCD REST API | 官方(`testnet.sentry.lcd.injective.network`) | 独立验证交易 memo 与执行状态(`code`/`block height`) |
| Injective 测试网区块浏览器 | 官方(`testnet.explorer.injective.network`) | 人工可视化核验交易 |

---

## 6. 示例任务与预期输出(真实测试网调用,非编造)

> 以下为对公网部署地址 `http://47.98.99.199:8000/api/analyze` 的真实调用结果,链上哈希已经过
> **三重独立验证**:(1)从 API 响应本地重算哈希,(2)拿重算的哈希与响应里的 `onchain.hash` 比对,
> (3)直接查询 Injective 测试网 LCD REST API 取出链上 memo 与之比对。

### 示例任务:多标的多因子策略构建 + 链上存证

**输入**
```json
{"text": "用 600519.SH 000858.SZ 构建稳健的多因子策略并回测", "riskLevel": "balanced"}
```

**协作轨迹**(15 步,末两步为本次新增):
```
Planner → Factor → Strategy → Backtest(3 轮反馈迭代)→ Backtest(基准对照)
→ Risk → Report → Advisor → Notary
```

**签文与建议**
```json
{
  "signature": "风云未定 · 观望为上",
  "plainAdvice": "本次策略年化收益 -45.2%,未跑赢买入持有基准(-25.5%),组合风险等级「medium」。",
  "actionHint": "watch",
  "actionHintCn": "宜观"
}
```

**链上存证结果**(API 响应原文)
```json
{
  "ok": true,
  "txHash": "D4648C4C1560BF04B1119D0B8AD2EBD33FF5C5CC8EF616BA28F2227216532334",
  "explorerUrl": "https://testnet.explorer.injective.network/transaction/D4648C4C1560BF04B1119D0B8AD2EBD33FF5C5CC8EF616BA28F2227216532334",
  "hash": "54b223749098ac9915fc40594cb266659cf02f956ef549a0f7a08c8f200f2e11"
}
```

**独立验证过程**(实测,非假设):

1. 从 API 响应本地重算哈希(用响应中的 `query`/`advice`/`report`/`disclaimer`/`backtest` 字段,
   与 `notary._canonical_payload` 同样的规则重新计算):
   ```
   recomputed hash: 54b223749098ac9915fc40594cb266659cf02f956ef549a0f7a08c8f200f2e11
   onchain.hash   : 54b223749098ac9915fc40594cb266659cf02f956ef549a0f7a08c8f200f2e11
   match: True
   ```

2. 直接查询 Injective 测试网 LCD REST API(`GET /cosmos/tx/v1beta1/txs/<txHash>`)取出链上原始数据:
   ```
   memo:  moneygod:v1:54b223749098ac9915fc40594cb266659cf02f956ef549a0f7a08c8f200f2e11
   code:  0   (执行成功,非仅广播受理)
   block: 134541414
   ```

三者完全一致 —— 从"AI 生成的结论"到"链上记录",全程可由任何第三方独立复现验证,不依赖对
MoneyGod 团队的信任。

---

## 7. 团队信息与成员分工

_待补充 —— 需要团队自行填写真实姓名与分工。_

---

## 8. Injective 报名表补充问题

以下三项为团队开发过程中的主观体验反思,需要团队自行填写(不应由 AI 代答):

**8.1 本次 Injective 开发过程中,是否遇到困难?**

_待补充。_

**8.2 Injective 的开发资料有哪些地方帮助到了您?**

_待补充。_

**8.3 Injective 的开发资料有哪些地方需要改进?**

_待补充。_

> 供参考,开发过程中客观遇到的技术细节(供团队回忆时参考,不代替主观作答):
> - 官方 `@injectivelabs/ainj` npm 包声明需要 Node >= 24,实际在 Node 22 环境下仍可正常安装运行(仅有 engine 警告)。
> - `injective-core` 依赖的 `injectived` 二进制下载脚本里硬编码的 GitHub Release 版本号(`v1.20.1`)与实际 Release tag 命名规则(带时间戳后缀,如 `v1.20.1-1782532109`)不一致,导致自动下载 404,需要手动下载正确 tag 的 release 包。
> - 官方文档列出的测试网水龙头 API 端点(`https://jsbqfdd4yk.execute-api.us-east-1.amazonaws.com/v1/faucet`)在实测中返回 404,最终改用官方水龙头网页(`https://testnet.faucet.injective.network/`)手动领取。
> - `pyinjective` 的 `SimulatedTransactionFeeCalculator` 默认 1.3x gas 缓冲在我们的消息+memo 组合场景下偶发不足(实测稳定短缺约 92 gas 单位,导致交易广播成功但执行失败 out-of-gas),需要手动调高到 1.4x。

---

## 9. 合规与安全说明

- 测试网密钥专款专用,不承载任何真实资产价值,私钥仅存在于服务器 `.env`(gitignored),从未出现在代码仓库或对话记录中。
- 与 PandaAI/度小满两条赛道共用同一套合规护栏(`compliance.py`):禁用措辞扫描 + 强制风险提示,链上存证功能不改变这一行为。
