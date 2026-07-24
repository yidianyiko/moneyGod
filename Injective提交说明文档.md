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

> 以下三项基于本项目实际开发过程中真实遇到的情况整理,细节均可对照本文档第 4/6 节与
> 代码仓库提交记录核实,不是套话。

**8.1 本次 Injective 开发过程中,是否遇到困难?**

遇到了几个比较具体的坑,按影响从大到小排:

1. **交易"广播成功"不等于"执行成功"**:`pyinjective` 的 `MsgBroadcasterWithPk`(以及我们
   手写的等价广播流程)默认走 `broadcast_tx_sync_mode`,拿到的 `code: 0` 只代表交易通过了
   CheckTx(进了 mempool),不代表它最终在区块里执行成功。我们最初的实现在这一点上判断过于
   乐观,直到用 LCD REST API 反查已确认的交易,才发现返回"成功"的交易实际以 `code: 11`
   (out of gas)执行失败——`SimulatedTransactionFeeCalculator` 默认的 1.3x gas 缓冲,在
   我们"自转账 + memo"这个具体消息组合下,稳定地差了约 92 个 gas 单位。调到 1.4x 后问题
   消失,但这提醒我们:凡是链上"存证"类场景,判断成功与否必须查最终执行结果,不能只信
   广播时的返回码。
2. **测试网水龙头的官方文档信息有误**:文档里给出的水龙头 API 端点
   (`https://jsbqfdd4yk.execute-api.us-east-1.amazonaws.com/v1/faucet`)实测直接返回
   404,最后是通过官方水龙头网页(`https://testnet.faucet.injective.network/`)手动领取
   测试币解决的。
3. **`injective-core` npm 包的二进制下载脚本有版本号 bug**:它按 `v{package.json版本号}`
   拼接 GitHub Release 下载链接,但 `InjectiveFoundation/injective-core` 仓库实际的 Release
   tag 命名规则带了一个时间戳后缀(如 `v1.20.1-1782532109`),导致自动下载 404。最后是手动
   找到匹配版本号的正确 tag、下载对应的 release 包解决的。
4. **本地开发环境的网络代理与 gRPC 握手偶发冲突**:开发机上配置的 HTTP/HTTPS 代理会间歇性
   导致对 Injective 测试网 gRPC 端点的 TLS 握手失败("Handshake read failed"),这个不算
   Injective 自身的问题,但排查花了一些时间才定位到是本地代理而非链上服务的问题。
5. **区块浏览器是纯客户端渲染的 SPA**:程序化验证(curl/自动化脚本)访问交易详情页会收到
   404,因为页面内容依赖浏览器端 JS 渲染,不是服务端直出。最终改用 Injective 测试网的
   LCD REST API 直接查询交易原始数据(memo、执行码、区块高度)完成自动化验证,人工用真实
   浏览器打开链接确认页面能正常渲染。

**8.2 Injective 的开发资料有哪些地方帮助到了您?**

- 官方 Python SDK(`pyinjective`)本身的源码是最可靠的参考——比如 `Network.testnet()` 里
  直接给出了真实可用的测试网 LCD/gRPC 端点、`chain_id`、`fee_denom` 等关键参数,不用东拼西凑。
- 官方通过 `@injectivelabs/ainj` 提供的 Claude Code Agent Skills(`injective-wallet-ops`、
  `injective-cli`、`injective-faucet` 等)给出了地址生成、编码转换(`inj1...` ↔ `0x...`)
  等具体可运行的代码模式,省去了自己摸索 bech32 编码细节的时间。
- 测试网水龙头 API 端点虽然本身失效了,但至少文档里存在这条信息,让我们知道"确实有 API
  形式的领水方式",促使我们进一步排查,而不是从一开始就走网页表单。
- LCD REST API 文档足够清晰完整,让我们能不依赖任何 SDK,仅凭 `curl` 就独立验证交易的
  memo 字段和执行状态——这对我们"链上可验证性"这个核心设计至关重要。

**8.3 Injective 的开发资料有哪些地方需要改进?**

- 测试网水龙头的 API 端点文档需要更新或加自动化检测——目前文档给出的地址实测是失效的
  (404),容易让新接入的开发者卡在第一步。
- `injective-core` npm 包的二进制下载逻辑应该按实际 Release tag 规则匹配版本(或至少在
  下载失败时给出更明确的排查指引),而不是假设 tag 名等于 package.json 里的版本号。
- 建议在 SDK 文档里明确提示"`broadcast_tx_sync_mode` 返回码只代表 CheckTx 通过,不代表
  最终执行成功",并给出正确的最终状态确认方式(查 LCD `tx_response.code`)——这是一个很
  容易踩、后果又不轻(以为存证成功实际失败)的坑,值得在文档里显著提示。
- `SimulatedTransactionFeeCalculator` 的默认 gas 缓冲倍数(1.3x)建议在文档里说明其经验
  安全边际,或针对常见消息类型给出推荐值,避免开发者需要靠实测试错才能发现默认值不够。
- 区块浏览器建议提供一个轻量的服务端直出/只读 API 方式查看单笔交易详情,方便自动化脚本或
  CI 流程做验证,而不必依赖浏览器 JS 渲染。

---

## 9. 合规与安全说明

- 测试网密钥专款专用,不承载任何真实资产价值,私钥仅存在于服务器 `.env`(gitignored),从未出现在代码仓库或对话记录中。
- 与 PandaAI/度小满两条赛道共用同一套合规护栏(`compliance.py`):禁用措辞扫描 + 强制风险提示,链上存证功能不改变这一行为。
