# 赛博财神庙 v2 实施计划（AI 求签 + 横屏像素 UI）

> **For agentic workers:** 执行本计划时 REQUIRED SUB-SKILL：`executing-plans` 或 `subagent-driven-development`。每个任务完成后勾选 `[x]` 并按任务说明提交 git。严禁跳过验证步骤。
>
> **Spec:** `docs/superpowers/specs/2026-07-25-cyber-fortune-v2-design.md`（已确认，commit 4f62f0f）

## 目标

把 T5AI-Board 上的 cyber_fortune 固件从"离线 60 签随机抽取"重构为：横屏 480×320 白底黑像素 UI + GIF 编排 + 触摸选类目/答问题 + 调用 core-engine 后端 LLM 生成签文 + 断网无缝本地兜底 + 按钮触发打印。

## 架构总览

```
[T5AI 固件]                                  [core-engine @ 47.98.99.199:8000]
scene_standby ─→ scene_category ─→ scene_question
      ↑                                   │
      │ 60s 超时                 POST /api/fortune/draw ──→ Starlette route
scene_result ←─ scene_draw ←──── (∥ thinking.gif 弹性等待)   └→ asyncio.to_thread(ask_json)
  [打印][再求一签]    └ 失败→fortune_fallback 本地签           └→ 火山方舟 LLM (8s budget)
```

- **固件新模块**（`apps/cyber_fortune/src/`，CMake `aux_source_directory` 自动收集，无需改 CMakeLists）：
  `fortune_flow.c`（状态机）、`fortune_net.c`（WiFi+HTTP）、`fortune_fallback.c`（本地兜底签）、`fortune_question.c`（本地题库）、`scene_standby.c`/`scene_category.c`/`scene_question.c`/`scene_draw.c`/`scene_result.c`、`fortune_printer.c`（改造）、`assets/`（GIF C 数组 + 字体 + GBK 表）
- **后端新模块**：`core-engine/src/fortune.py` + 路由挂在 `src/a2a_server/app.py` 的 `build_app()`

## 关键技术事实（已验证）

| 事实 | 出处 |
|---|---|
| 横屏 = 改 `BOARD_LCD_ROTATION`（`TUYA_DISPLAY_ROTATION_0`→`90`或`270`，方向需实机验证） | `boards/T5AI/TUYA_T5AI_BOARD/tuya_t5ai_ex_module.h` L49 |
| LVGL 9.1 core 自动按 rotation 变换触摸坐标，indev 层无需改 | `src/liblvgl/v9/lvgl/src/indev/lv_indev.c` L675-683 |
| GIF 播放：`LV_IMG_DECLARE(x); img=lv_gif_create(parent); lv_gif_set_src(img,&x);`，`LV_USE_GIF=1` 默认开 | `examples/graphics/lvgl_gif/` |
| HTTP：`http_client_request(&req,&rsp)`，cacert=NULL 走明文 HTTP，用完 `http_client_free` | `src/libhttp/include/http_client_interface.h` |
| WiFi 初始化顺序：`tal_kv_init→tal_sw_timer_init→tal_workq_init→tal_event_subscribe(EVENT_LINK_STATUS_CHG)→TUYA_LwIP_Init→netmgr_init(NETCONN_WIFI)→netmgr_conn_set(...CMD_SSID_PSWD...)` | `examples/protocols/http_client/src/example_http_client.c` |
| 联网需在 app config 加 LWIP 符号（见任务 3.2） | `examples/protocols/http_client/app_default.config` |
| 后端是 **Starlette**（a2a-sdk），路由用 `app.router.routes.append(Route(...))`，同步 LLM 用 `asyncio.to_thread` | `core-engine/src/a2a_server/app.py` 的 `api_analyze` |
| `ask_json()` 已存在于 `src/llm.py`；OpenAI client 当前**无 timeout**，需加 | `core-engine/src/llm.py` |
| Flash 预算：primary_ap_app 3808KB，现用 ~1.37MB，余 ~2.3-2.4MB；素材超了就砍 GIF 帧/待机变体 | `size_map_total.csv` |
| GIF 实测：opening_1=3f/0.6s(1536×1024彩)；opening_2~6=10f/4.0s(480×320)；thinking=10f/4.0s；getting_lottery=4f/1.2s(1016×677彩)；lottery_get=9f/1.8s | 自测 |
| 打印机 GBK 表生成器已有，需扩到全 GB2312 | `t5-dev/tools/gen_gbk_map.py` |
| 构建：`cf_build.sh` / 烧录 `cf_flash.sh` / 监视 `cf_monitor.sh`（460800）；换 config 后需 clean | `t5-dev/` |

---

## Phase 1：后端 `/api/fortune/draw`（可独立验证，先做）

### Task 1.1 LLM 超时支持
- [ ] `core-engine/src/llm.py`：`client()` 的 `OpenAI(...)` 增加 `timeout` 参数支持——`ask()`/`ask_json()` 增加可选 `timeout: float | None = None` 形参，透传到 `client().with_options(timeout=timeout)`（OpenAI SDK 原生支持）。不改变现有调用方行为（默认 None）。
- [ ] 验证：`cd core-engine && python -c "from src.llm import ask_json; print('ok')"`

### Task 1.2 fortune 业务模块
- [ ] 新建 `core-engine/src/fortune.py`：
  - `GRADES = [("上上",5,0.15),("上",4,0.30),("中",3,0.35),("下",2,0.15),("下下",1,0.05)]`，`draw_grade()` 服务端加权随机
  - `LOT_NO = random 1..100`（形式感，与内容无关）
  - `generate_fortune(category, question, answer) -> dict`：先 `draw_grade()`，再拼 prompt 调 `ask_json(prompt, model=SEED_MODEL, timeout=8)`；prompt 要求：白话四句签诗（每句 7~10 字）、针对 question+answer 的解签（60~90 字）、行动建议（30~50 字）；**严格要求只用常用汉字（GB2312 一级字库），禁生僻字**；语气 = 赛博财神、俏皮但不轻浮；**禁止成语典故套话**
  - 返回 `{lot_no, grade, grade_score, poem(list[4]), explanation, advice}`
  - LLM 输出缺字段/解析失败 → raise，由路由层兜底
- [ ] 服务端兜底：`fortune.py` 内置 `FALLBACK = {category: {grade: {...}}}`（每类每档 1 条，共 30 条，可先写 6 条通用 + 按 grade 复用），LLM 失败时返回兜底并打日志
- [ ] 验证：`python -c "from src.fortune import draw_grade; print([draw_grade()[0] for _ in range(10)])"`

### Task 1.3 路由 + token 鉴权
- [ ] `core-engine/src/config.py`：加 `get_fortune_token()`（读 `FORTUNE_API_TOKEN`，默认 `""` 表示不校验）
- [ ] `core-engine/src/a2a_server/app.py` 的 `build_app()`：仿照 `api_analyze` 增加：
  ```python
  async def api_fortune_draw(request):
      token = get_fortune_token()
      if token and request.headers.get("X-Fortune-Token") != token:
          return JSONResponse({"error": "unauthorized"}, status_code=401)
      try:
          body = await request.json()
      except Exception:
          return JSONResponse({"error": "invalid json"}, status_code=400)
      category = str(body.get("category", "今日运势"))
      question = str(body.get("question", ""))
      answer = str(body.get("answer", ""))
      result = await asyncio.to_thread(generate_fortune, category, question, answer)
      return JSONResponse(result)
  app.router.routes.append(Route("/api/fortune/draw", api_fortune_draw, methods=["POST"]))
  ```
- [ ] `.env.example` 加 `FORTUNE_API_TOKEN=`
- [ ] 测试 `core-engine/tests/test_fortune.py`：`draw_grade` 分布（1000 次全部落在 5 档）、`FALLBACK` 结构完整（6 类×5 档可取到）、mock `ask_json` 后 `generate_fortune` 字段齐全 / 解析失败走兜底
- [ ] 验证：`cd core-engine && python -m pytest tests/test_fortune.py -v`
- [ ] 本地起服务 curl 冒烟：`python serve.py &` 然后
  `curl -s -X POST localhost:8000/api/fortune/draw -H 'Content-Type: application/json' -d '{"device_id":"dev","category":"财运","question":"最近在纠结什么","answer":"要不要换工作"}' | python -m json.tool`
  预期：200 + 全部 6 字段；无 ARK key 时应返回兜底而非 500
- [ ] **commit**: `feat(core-engine): /api/fortune/draw AI 求签接口`
- [ ] （部署）用户确认后再部署到 47.98.99.199

---

## Phase 2：素材流水线（本机工具链，与 Phase 1 并行可做）

### Task 2.1 GIF 预处理 + 转 C 数组
- [ ] 新建 `t5-dev/tools/prep_gifs.sh`：
  - `opening_1_01.gif`(1536×1024) 与 `getting_lottery.gif`(1016×677) 用 `sips`/`ffmpeg` 缩放到 480×320（若无 ffmpeg 则装 `brew install ffmpeg`，或用 gifsicle）
  - 所有 GIF 统一输出到 `t5-dev/assets_build/gif/`
- [ ] 用 LVGL 官方脚本 `t5-dev/TuyaOpen/src/liblvgl/v9/lvgl/scripts/LVGLImage.py`（GIF 直接以 raw 格式打包）或参照 `examples/graphics/lvgl_gif` 中现成 `tuya_gif.c` 的格式：GIF 在 LVGL9 中以**原始文件字节数组**形式声明（`lv_image_dsc_t` + `LV_COLOR_FORMAT_RAW`）。写 `t5-dev/tools/gif2c.py`：读 .gif 二进制 → 生成 `gif_<name>.c`（`const uint8_t` 数组 + `lv_image_dsc_t`），输出到 `apps/cyber_fortune/src/assets/`
- [ ] 体积核算：8 个 GIF 原始字节总和，若 > 1.6MB：砍待机变体（opening_2~6 保留 3 个）并记录
- [ ] 验证：`ls -la apps/cyber_fortune/src/assets/gif_*.c && du -ch` 总量在预算内
- [ ] **commit**: `feat(t5): GIF 素材转 C 数组流水线`

### Task 2.2 Fusion Pixel 字体（GB2312 一级）
- [ ] 下载 Fusion Pixel 12px 等宽 otf（`fusion-pixel-12px-monospaced-zh_hans`，开源 OFL）到 `t5-dev/fontbuild/`
- [ ] `npm i -g lv_font_conv`（或项目内 npx）
- [ ] 写 `t5-dev/tools/gen_gb2312_range.py`：输出 GB2312 一级 3755 字 + ASCII + 常用标点的 unicode range 文件
- [ ] 生成两档：
  - `font_px24.c`：`lv_font_conv --font fusion-pixel-12px-monospaced-zh_hans.otf --size 24 --bpp 1 --format lvgl --lv-include lvgl.h -r 0x20-0x7E -r <GB2312一级> --no-compress -o font_px24.c`（bpp 1 保持像素锐利）
  - `font_px36.c`：同上 `--size 36`（标题/签名档用）
- [ ] 体积核算：bpp1 时 24px ≈ 3755×72B ≈ 280KB，36px 若超预算则只收窄字符集（标题只需 "上下中大吉签第0-9财运事业姻缘学业健康今日运势" 等 ~50 字）——**决策：36px 用窄字集（~10KB），24px 用全一级字库**
- [ ] 拷入 `apps/cyber_fortune/src/assets/`
- [ ] 验证：文件生成且含 `lv_font_t font_px24;`
- [ ] **commit**: `feat(t5): Fusion Pixel GB2312 字体`

### Task 2.3 打印机全 GBK 映射表
- [ ] 改 `t5-dev/tools/gen_gbk_map.py`：不再扫 qianpu_data.c；直接遍历 GB2312 全集（`for hi in 0xA1..0xF7 for lo in 0xA1..0xFE`，decode 'gbk' 成功即收录），输出 `fortune_gbk_map.c`（~7445 条 ×4B ≈ 30KB）
- [ ] 验证：`python3 t5-dev/tools/gen_gbk_map.py` 输出条目数 ≈7400+，文件编译格式与旧版一致
- [ ] **commit**: `feat(t5): 全 GB2312 打印机 GBK 映射表`

---

## Phase 3：固件基础改造（横屏 + 联网配置）

### Task 3.1 横屏
- [ ] `boards/T5AI/TUYA_T5AI_BOARD/tuya_t5ai_ex_module.h` L49：`BOARD_LCD_ROTATION` 改为 `TUYA_DISPLAY_ROTATION_90`（若实机方向反了改 270）。触摸无需改（LVGL core 自动变换）
- [ ] 最小验证固件：临时在现有 UI 上画一个 480 宽的横向标签，`cf_build.sh && cf_flash.sh`，实机确认横屏方向与触摸对齐；方向错则换 270 重刷
- [ ] **commit**: `feat(t5): LCD 横屏 480×320`

### Task 3.2 app config 加联网符号
- [ ] `apps/cyber_fortune/config/TUYA_T5AI_BOARD_LCD_3.5.config` 追加（对齐 http_client 示例 app_default.config）：
  `CONFIG_MEM_SIZE=51200`、`CONFIG_MEMP_NUM_UDP_PCB=10`、`CONFIG_MEMP_NUM_TCP_SEG=80`、`CONFIG_PBUF_LINK_ENCAPSULATION_HLEN=96`、`CONFIG_TCP_SND_BUF=32768`、`CONFIG_TCP_SND_QUEUELEN=44`、`CONFIG_MEMP_NUM_NETBUF=32`、`CONFIG_DEFAULT_UDP_RECVMBOX_SIZE=24`、`CONFIG_MEMP_NUM_SYS_TIMEOUT=12`、`CONFIG_LWIP_EAPOL_SUPPORT=0`、`CONFIG_LWIP_TX_PBUF_ZERO_COPY=0`、`CONFIG_CONFIG_TUYA_SOCK_SHIM=0`、`CONFIG_LWIP_DHCPC_STATIC_IPADDR_ENABLE=1`、`CONFIG_ETHARP_SUPPORT_STATIC_ENTRIES=1`、`CONFIG_LWIP_NETIF_STATUS_CALLBACK=1`、`CONFIG_LWIP_TIMEVAL_PRIVATE=0`、`CONFIG_IN_ADDR_T_DEFINED=y`
- [ ] `tos.py config choice -c TUYA_T5AI_BOARD_LCD_3.5` 后 **clean 重建**，确认编译过
- [ ] **commit**: `feat(t5): 联网 LWIP 配置`

### Task 3.3 fortune_net.c（WiFi + HTTP 客户端）
- [ ] 新建 `apps/cyber_fortune/include/fortune_net.h` / `src/fortune_net.c`：
  - `fortune_net_init(void)`：按 http_client 示例顺序初始化，SSID/密码/服务器地址/token 用 `#define`（顶部集中，注明烧录前改）；订阅 `EVENT_LINK_STATUS_CHG` 维护 `s_net_up` 标志
  - `fortune_net_is_up(void)`
  - `fortune_net_draw(const char *category, const char *question, const char *answer, fortune_result_t *out)`：cJSON 组包 → `http_client_request`（POST /api/fortune/draw，Header `X-Fortune-Token`，timeout_ms=10000）→ cJSON 解析 6 字段到 `fortune_result_t{lot_no; grade[8]; grade_score; poem[4][64]; explanation[512]; advice[256];}` → `http_client_free`；任一步失败返回 OPRT 错误码（不 crash）
  - **在独立 worker 线程跑**（仿 fortune_printer 的线程+信号量模式），对外 `fortune_net_draw_async(req, done_cb)`，回调在 net 线程触发，UI 侧只轮询结果标志（scene_draw 每帧检查）
- [ ] 验证：编译过；实机连上 WiFi 后日志出现 link up
- [ ] **commit**: `feat(t5): fortune_net WiFi+HTTP`

---

## Phase 4：固件业务模块（数据 → 场景 → 状态机）

### Task 4.1 数据层：题库 + 本地兜底签
- [ ] `include/fortune_data.h`：`fortune_result_t`（同上）、`fortune_category_t` 枚举（财运/事业/姻缘/学业/健康/今日运势）
- [ ] `src/fortune_question.c` + `include/fortune_question.h`：每类 3~5 道追问题，每题 3~4 个选项；`fortune_question_pick(category, question_t *out)` 随机取一道。文案要求：口语化、能引出用户具体处境（如财运："你最近的钱主要花在哪？" 选项：吃喝玩乐/投资理财/生活刚需/存着不动）
- [ ] `src/fortune_fallback.c`：6 类×5 档各 1 条完整签文（grade 加权随机同后端 15/30/35/15/5），`fortune_fallback_draw(category, result_t *out)`；文案风格与后端 prompt 一致（白话诗+解签+建议，禁成语典故）
- [ ] 验证：编译过 + 桌面侧目视 review 文案
- [ ] **commit**: `feat(t5): 题库与本地兜底签数据`

### Task 4.2 场景框架 + UI 公共样式
- [ ] `include/fortune_flow.h`：场景枚举 `SCENE_BOOT/STANDBY/CATEGORY/QUESTION/DRAW/RESULT`、`fortune_flow_goto(scene)`、每场景 `void scene_xxx_enter(lv_obj_t *root)` / `scene_xxx_exit(void)` 约定
- [ ] `src/fortune_ui_style.c`：白底黑字全局主题（`lv_obj_set_style_bg_color(scr, lv_color_white(),0)`）、8px 粗黑边框按钮样式、字体 `font_px24`/`font_px36` extern；按钮按下反色（黑底白字）
- [ ] `src/fortune_flow.c`：状态机核心——单一 `lv_obj_t *s_root` 容器切换；60s 无操作 lv_timer → 任意场景（除 BOOT）硬切 STANDBY；全局单个 `lv_gif` 对象复用
- [ ] `tuya_main.c` 改造：保留现有硬件初始化（USB 打印机等），新增 `fortune_net_init()`，`fortune_ui_init` 替换为 `fortune_flow_start()`（BOOT 场景）
- [ ] 验证：编译过
- [ ] **commit**: `feat(t5): 场景状态机骨架`

### Task 4.3 各场景实现（按 GIF 编排，spec 已确认）
- [ ] `scene_standby.c`：BOOT 播 opening_1 ×5 循环（3s，并行等 WiFi/USB）→ STANDBY 用 opening_2~6 随机洗牌轮播（每个播完 1 遍 4s 换下一个，一轮播完重新洗牌）；全屏任意触摸 → CATEGORY
- [ ] `scene_category.c`：静态页，2×3 六宫格按钮（财运/事业/姻缘/学业/健康/今日运势），顶部标题 font_px36；点击 → 存 category → QUESTION
- [ ] `scene_question.c`：静态页，`fortune_question_pick` 取题；题干 + 3~4 个全宽选项按钮；点击 → 存 answer → DRAW
- [ ] `scene_draw.c`：进入即 `fortune_net_draw_async`（net down 或 10s 超时→fallback）；GIF 编排：thinking 至少 1 整循环(4s)，之后每帧(400ms)检查结果就绪 → getting_lottery ×2(2.4s) → lottery_get ×1(1.8s)+0.5s 停帧 → RESULT。thinking 播了 3 循环(12s)还没结果 → 直接用 fallback 进入 finale
- [ ] `scene_result.c`：布局（横屏 480×320）：左侧竖排大字 grade 徽章(font_px36 反白块) + 第 N 签；右侧签诗四句(font_px24) + 滚动容器放解签/建议；底部两按钮 `[打印签文]`（→ fortune_printer，打印中置灰防重按）`[再求一签]`（→ CATEGORY）
- [ ] 每场景做完即编译；全部完成后 flash 实机走一遍全流程（先拔网线走 fallback 路径）
- [ ] **commit**（可分多次）: `feat(t5): scene_* 五场景实现`

### Task 4.4 打印改造
- [ ] `fortune_printer.c`：小票模版改为新结构——标题"赛博财神庙"/第 N 签·{grade}/签诗四句/解签/建议/落款；数据源从 `qianpu_entry_t` 换成 `fortune_result_t`；GBK 映射换用新全量表（接口 `g_gbk_map` 不变，无需改查表逻辑）
- [ ] 删除旧文件：`qianpu_data.c/.h`、旧 `fortune_ui.c`、旧字体 C 文件（确认无引用后删）
- [ ] 验证：编译过 + 实机打印一张 fallback 签
- [ ] **commit**: `feat(t5): 打印新签文格式 + 移除旧资产`

---

## Phase 5：联调与收尾

### Task 5.1 全链路联调
- [ ] 后端部署到 47.98.99.199（用户确认后），板端 `#define` 填真实 token/WiFi
- [ ] 实机在线路径：选类目→答题→动画期间日志确认 HTTP 200→结果页字段完整→打印
- [ ] 断网路径：关路由/错 SSID → fallback 无缝出签（用户无感知差异）
- [ ] 慢响应路径：后端加 sleep 模拟 6s → 动画弹性等待正常衔接
- [ ] 60s 超时回待机、再求一签回类目页
- [ ] Flash 占用最终核对：`size_map_total.csv`，超限则按 Task 2.1 预案裁剪
### Task 5.2 收尾
- [ ] 使用 `verification-before-completion` 技能核验后再声明完成
- [ ] **commit**: `feat(t5): cyber fortune v2 联调完成`
- [ ] 使用 `finishing-a-development-branch` 技能决定合并方式

## 风险与预案
1. **Flash 超预算**（最大风险）：GIF 优先降帧（opening_2~6 从 10f→6f）、待机变体 5→3、36px 字体窄字集。逐项砍到 `size_map_total.csv` 通过为止。
2. **lv_gif 解码 480×320 GIF 性能不足**：实测帧率；卡顿则预缩 GIF 调色板至 16 色 + 降帧。
3. **旋转方向反**：90/270 各试一次，10 分钟内解决。
4. **LLM 输出生僻字**：prompt 约束 + 服务端过滤（非 GB2312 一级字符替换为同义常用字/剔除），板端字体缺字显示空白可接受为兜底。
