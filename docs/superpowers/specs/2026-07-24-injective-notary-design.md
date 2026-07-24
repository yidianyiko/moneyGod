# Injective 链上签文存证（Notary Agent）设计

## 背景 / 目标

MoneyGod 现有的 Multi-Agent 量化投研引擎（PandaAI 18 + 度小满 15 双赛道）需要扩展以同时参与
Injective「AI x Blockchain 创新赛道」（赛道 01）。赛道硬性要求：项目须实际部署/集成 Injective
网络（测试网或主网均可），并展示区块链技术在产品中的真实应用。

选定方向：**签文上链存证**——每次多 Agent 工作流跑完后，把"签文 + 建议 + 报告 + 关键回测指标
+ 免责声明"的哈希，广播一笔带 memo 的 Injective 测试网交易锚定下来，做成不可篡改的 AI 决策
存证。叙事上和现有"玄学皮·量化芯"的反差感天然契合：AI 说的话有链上凭证，赖不掉。

## 架构

在 `research_workflow()`（`core-engine/src/workflow.py`）末尾，Report 之后新增两个环节：

```
...(现有) Planner → Factor → Strategy ⇄ Backtest 反馈闭环 → Risk → Report
  │
  ▼
Advisor Agent   （从 tasks.py 挪进来，纯函数，产出签文/建议/action 判定，已实现于 src/advisor.py）
  │
  ▼
Notary Agent    （新）
  对 {query, riskLevel, advice, report, disclaimer, backtest 关键指标, 时间戳} 做规范化 JSON + sha256
  → 用 pyinjective 构建/签名/广播一笔 MsgSend（自转账给自己）到 Injective 测试网
  → memo = "moneygod:v1:<hash>"
  → 返回 {ok, txHash, explorerUrl, hash} 或 {ok:false, reason}（失败绝不阻断主流程）
  │
  ▼
返回：QuantConclusion + advice + onchain{...} + trace（含 Advisor/Notary 两步）
```

## 组件变更

### 新增 `core-engine/src/notary.py`
- `_canonical_payload(result: dict) -> dict`：抽取待存证字段，key 排序，构造确定性 JSON
- `_hash_payload(payload: dict) -> str`：`hashlib.sha256(...).hexdigest()`
- `anchor(result: dict) -> dict`：
  - 懒加载 `pyinjective` 客户端（沿用 `data_adapter._ensure_init()` 的懒初始化风格）
  - 从 `INJECTIVE_PRIVATE_KEY`（hex，测试网专用密钥）读取签名密钥
  - 构建 `MsgSend`（发送方=接收方=自己，最小金额，仅用于携带 memo 上链）
  - 广播到 Injective 测试网（chain-id / LCD / gas 参数以 `injective-cli` skill 与
    `pyinjective` 官方 testnet 帮助函数为准，不手写猜测值）
  - 8-10 秒超时；任何异常（余额不足/网络问题/超时）一律 catch，返回 `{ok:false, reason}`，
    绝不抛出、绝不拖垮/阻断本次分析结果
  - 返回 `explorerUrl` 指向 Injective 测试网区块浏览器的交易详情页

### `core-engine/src/workflow.py`
- Report 步骤之后，依次调用 `advisor.to_advice(result)` 与 `notary.anchor(result)`
- 各自追加一条 trace：`Advisor`（生成签文）、`Notary`（上链存证，成功/失败均记录一行）
- 结果字段：`result["advice"]`、`result["onchain"]`

### `core-engine/src/tasks.py`
- 删除此前临时加在 `handle_task` / `analyze_structured` 里的 `advisor.to_advice()` 调用
  （现在 workflow.py 内部统一处理，避免重复/顺序不一致）
- `_render_workflow()`：签文段落下方新增一行链上存证状态
  - 成功：`⛓️ 链上存证:已锚定 Injective 测试网,交易哈希 <txHash>（<explorerUrl>）`
  - 失败：`⛓️ 链上存证:本次未能上链,不影响分析结果（<reason>）`

### Web UI（`core-engine/src/a2a_server/web/index.html`）
- 签文卡片（`.fortune`）下方新增一个小模块，展示同样的成功/失败两态，成功时交易哈希做成可
  点击链接指向区块浏览器

### Agent Card（`core-engine/src/a2a_server/card.py`）
- 现有 skill 描述补一句：结果哈希锚定在 Injective 测试网，可独立验证不可篡改

### 环境变量
- `INJECTIVE_PRIVATE_KEY`（hex，无 `0x` 前缀）：新增到 `.env.example`（占位）与本地/服务器的
  `.env`（真实值，绝不进 Git、绝不出现在对话或日志里）
- 密钥专款专用，仅用于测试网签名，不承载任何真实资产价值

## 技术选型说明

- **官方工具用于设置与核对，不是运行时依赖**：`@injectivelabs/ainj` 装好的官方
  skills（`injective-wallet-ops`/`injective-faucet`/`injective-cli`）和本地 `injectived`
  二进制，用来在实现阶段核对钱包生成方式、地址编码、测试网端点/gas 参数是否正确。
- **运行时签名/广播用 `pyinjective`（Python）**：与 core-engine 现有纯 Python 技术栈一致，
  不需要在生产服务器上额外部署 230MB 的 `injectived` 二进制 + `libwasmvm.so`，降低部署面。
- **上链方式：`MsgSend` + memo 哈希**，不写自定义 CosmWasm/EVM 合约——在黑客松时间约束下，
  这是能"真实广播一笔可验证测试网交易"且风险最低的路径；后续如有时间可升级为自定义合约
  存证（`injective-evm-developer` skill 已经装好，留作 stretch goal）。

## 数据流与响应时间

Injective 亚秒级出块，`anchor()` 的网络往返预计在数秒级，加在现有 30 秒~2 分钟的工作流响应
时间上，仍远低于 PandaAI 赛道 20 分钟的响应上限约束。

## 测试计划

1. 生成全新测试网密钥对，通过官方 faucet（尝试自动调用其 API；如遇验证码拦截则请用户手动领取）
2. 用假 payload 跑通 `notary.anchor()`，在 Injective 测试网区块浏览器上核实交易与 memo
3. 跑一次完整的示例任务，确认 `onchain.ok=true`，且能用响应里的字段重新计算哈希、和链上 memo
   对上（这是"可验证"的关键，需要在提交文档里写清楚验证方法）
4. 重新部署到 `47.98.99.199`，公网复测（包括 Web UI 新模块的展示）

## 非目标 / 明确不做

- 不做真实链上交易执行（订单簿下单等）——不动现有 A 股/基金资产范围，只做存证这一层
- 不写自定义智能合约（CosmWasm/EVM）作为 MVP——留作时间允许时的加分项
- 不修改 `ainj` 写入的全局配置——已确认仅落在项目本地 `.claude/settings.json` 与 `.ainj/`

## 需要用户提供（不可代答/代做的部分）

- Injective 提交材料里的团队信息、选手信息、开发过程反思问答
- 演示视频录制
- 若 faucet API 自动领取失败，需要手动走官方水龙头网页
