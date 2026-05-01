# YDWE AI 辅助开发功能设计

## 概述

为 YDWE 添加 AI 辅助开发功能，提升 Jass/Lua 地图开发体验。

## 核心功能

### 1. 代码补全 (Code Completion)
- **Jass 补全**: 原生函数、YDWE 扩展函数、自定义函数
- **Lua 补全**: 标准库、YDWE API、全局变量
- **上下文感知**: 根据当前代码上下文提供相关建议

### 2. AI 代码生成 (AI Code Generation)
- **自然语言转代码**: "创建一个触发器，当单位死亡时奖励金币"
- **代码解释**: 解释复杂代码逻辑
- **代码优化建议**: 识别性能瓶颈并提供优化方案

### 3. 实时错误检测 (Realtime Error Detection)
- **语法检查**: 实时标记语法错误
- **类型检查**: Jass 类型不匹配检测
- **逻辑分析**: 检测常见逻辑错误 (如空引用、无限循环)

## 技术架构

### 方案 A: Language Server Protocol (LSP)

```
┌─────────────────┐     LSP      ┌──────────────────┐
│   YDWE Editor   │◄────────────►│  YDWE LSP Server │
│  (DuiLib/Web)   │   WebSocket  │   (C++/Python)   │
└─────────────────┘              └──────────────────┘
                                          │
                                          ▼
                              ┌─────────────────────┐
                              │   AI Service        │
                              │ (OpenAI/Local LLM) │
                              └─────────────────────┘
```

**优势**:
- 标准化协议，可扩展性强
- 与编辑器解耦，支持多种前端
- 支持 VS Code 等外部编辑器

**核心组件**:

#### 1. LSP Server (ydwe-lsp)
```cpp
// 简化架构
namespace ydwe::lsp {
    class Server {
    public:
        // 初始化
        void initialize(const nlohmann::json& params);
        
        // 代码补全
        CompletionList completion(const TextDocumentPosition& pos);
        
        // 诊断
        std::vector<Diagnostic> validate(const std::string& code);
        
        // AI 功能
        std::string generateCode(const std::string& description);
    };
}
```

#### 2. Jass/Lua 解析器
```cpp
namespace ydwe::lsp::parser {
    // Jass 语法树
    class JassAST {
    public:
        struct Function {
            std::string name;
            std::vector<Parameter> params;
            Type returnType;
            Location location;
        };
        
        struct Variable {
            std::string name;
            Type type;
            bool isGlobal;
        };
        
        std::vector<Function> functions;
        std::vector<Variable> variables;
    };
    
    // 解析器
    class Parser {
    public:
        JassAST parse(const std::string& source);
        std::vector<Diagnostic> getErrors();
    };
}
```

#### 3. AI 服务接口
```cpp
namespace ydwe::lsp::ai {
    // AI 服务接口
    class AIService {
    public:
        virtual ~AIService() = default;
        
        // 代码生成
        virtual std::string generateCode(
            const std::string& prompt,
            const Context& context) = 0;
        
        // 代码解释
        virtual std::string explainCode(
            const std::string& code) = 0;
        
        // 优化建议
        virtual std::vector<Suggestion> suggestOptimizations(
            const std::string& code) = 0;
    };
    
    // OpenAI 实现
    class OpenAIService : public AIService {
        // 使用 OpenAI API
    };
    
    // 本地 LLM 实现
    class LocalLLMService : public AIService {
        // 使用本地模型 (如 llama.cpp)
    };
}
```

### 方案 B: 嵌入式 AI (直接集成到 YDWE)

```
┌─────────────────────────────────────────┐
│              YDWE Editor                │
│  ┌─────────────┐    ┌──────────────┐  │
│  │   Lua JIT   │    │   AI Module  │  │
│  │  (Parser)   │◄──►│  (llama.cpp) │  │
│  └─────────────┘    └──────────────┘  │
└─────────────────────────────────────────┘
```

**优势**:
- 无外部依赖
- 低延迟
- 离线工作

**劣势**:
- 模型大小限制
- 功能受限

## 实现状态

### 第一阶段: 基础 LSP ✅ (已完成)

LSP 服务器 (`ydwe-lsp.exe`) 和客户端插件 (`lsp_client.dll`) 已实现并集成到 YDWE 编辑器中。

**已实现功能**:
- ✅ Jass 词法分析器 (`jass_lexer.cpp`)
- ✅ Jass 语法分析器 (`jass_parser.cpp`)
- ✅ 代码补全 — 关键字、类型、BJ 函数、用户定义符号
- ✅ 跳转到定义 — 函数、全局/局部变量、参数
- ✅ Hover — 函数签名、变量类型
- ✅ 文档符号 — 函数和全局变量大纲
- ✅ 诊断 — 解析错误、重复定义检测
- ✅ 文档同步 — didOpen/didChange/didClose/didSave

**LSP 集成**:
- ✅ LSP 客户端插件通过 Lua FFI 加载 (`lsp_client.dll`)
- ✅ 通过 stdin/stdout JSON-RPC 通信
- ✅ 编辑器启动时自动启动 LSP 服务器

### 第二阶段: AI 集成 (进行中 — 2026-05-01)

#### Agent 通信层
- [x] YDTrigger Agent API — 18 个 C 导出函数（ECA 读写 + 物体编辑器）
- [x] JSON-RPC 服务端 — TCP 127.0.0.1:27118，后台线程
- [x] Python 测试客户端 — ydagent_client.py
- [x] 单元测试 — Catch2，18 用例，171 断言

#### AI 服务接口
- [x] 设计 AI 服务接口 (支持 OpenAI / Claude / 本地 LLM)
- [x] 实现请求构建与配置层 (`YDAgentAI.lua`)
- [x] 实现 AI 响应解析与安全操作校验
- [x] 实现最小编辑器 UI 集成
- [ ] 提示工程优化与用例模板扩展

#### 功能完善
- [x] 安全操作应用层 dry-run / confirm 流程
- [x] AI 操作计划生成入口 (自然语言 → 安全操作计划)
- [x] AI 代码生成入口 (自然语言 → Jass/Lua 文本)
- [x] 代码解释入口
- [x] GUI 触发器摘要与提示构建
- [x] 上下文窗口压缩与多触发器摘要
- [x] 多触发器关联分析

### 第三阶段: GUI 触发器 AI 助手 (进行中 — 2026-05-01)

#### 进度
1. ✅ YDTrigger Agent API — 18 个 C 导出函数（ECA 读写 + 物体编辑器二进制↔JSON）
2. ✅ Lua 封装 (`YDAgentCore.lua`) + JSON-RPC 服务端 (`YDAgentServer.lua`)
3. ✅ 物体编辑器 API — w3u/w3a/w3t/w3b/w3q 二进制解析与写入
4. ✅ 单元测试 — Catch2, 18 用例, 171 断言
5. ✅ 物体编辑器属性映射（field_id 到中文名的 SLK 表查询）
6. ✅ AI 模型接口配置层（Claude / OpenAI / 本地 LLM）
7. ✅ AI 模型响应解析与安全操作校验
8. ✅ GUI 触发器摘要与 AI 提示构建
9. ✅ 最小编辑器 UI 集成与用户确认流程
10. ✅ 安全操作应用层 dry-run / confirm 流程
11. ✅ Review Panel 审阅窗口
12. ✅ AI 操作计划生成入口
13. ✅ 多触发器上下文压缩
14. ✅ 交互式生成面板
15. ✅ AI 代码生成与触发器解释入口
16. ✅ 多触发器关联分析
17. 待完成: 构建完整触发器 ↔ 自然语言转换器

#### 2026-05-01 增量实现

- `Component\plugin\YDAgentFieldMap.lua`
  - 读取 `Component\share\zh-CN\mpq\units\*metadata.slk` 与 `WorldEditStrings.txt`。
  - 提供 `field_id -> 中文名/分类/类型/source` 映射。
  - 支持 `unit`、`item`、`ability`、`buff`、`doodad`、`destructable`、`upgrade` 等来源过滤。
- `Component\plugin\YDAgentAI.lua`
  - 提供 `claude`、`openai`、`local`/`local_llm` provider 配置。
  - API key 默认从 `ANTHROPIC_API_KEY` / `OPENAI_API_KEY` 读取，不硬编码密钥。
  - 支持 `ai.build_request` dry-run，便于前端预览即将发送的数据。
- `Component\plugin\YDAgentServer.lua` / `YDAgentServerWorker.lua`
  - JSON-RPC 服务保持 `127.0.0.1:27118`。
  - Loader 与 Worker 拆分，降低主线程代码复杂度。
  - RPC 错误统一返回 JSON-RPC 标准错误码：`-32700`、`-32600`、`-32601`、`-32602`、`-32603`。
  - 新增 `object.field_name`、`object.field_info`、`object.field_map`、`object.read_annotated`。
  - 新增 `ai.configure`、`ai.status`、`ai.build_request`、`ai.complete`。

#### 2026-05-01 下一阶段增量实现

- `Component\plugin\YDAgentOps.lua`
  - 定义 AI 返回操作白名单，禁止任意文件写入或任意代码执行。
  - 支持校验 `set_trigger_name`、`set_trigger_disabled`、`set_eca_func_name`、`set_eca_active`、`set_eca_param_value`、`add_eca`、`remove_eca`、`object_set_field`。
  - 输出 `ok`、`operations`、`warnings`、`errors`，其中删除 ECA 和物编字段修改会标记为需要用户复核。
- `Component\plugin\YDAgentAI.lua`
  - 新增 OpenAI / Claude / 本地 LLM 响应文本提取。
  - 支持从纯 JSON 或 Markdown fenced JSON 中提取操作计划。
  - 新增 `ai.parse_response`、`ai.validate_plan`、`ai.operation_schema`。
- `Component\plugin\YDAgentServerWorker.lua`
  - 新增 `agent.summarize_trigger(index)`，输出触发器 ECA 摘要。
  - 新增 `agent.build_prompt(index, instruction)`，生成包含触发器上下文和操作白名单的 AI 提示。
  - 新增 RPC 方法 `ai.parse_response`、`ai.validate_plan`、`ai.operation_schema`。
  - 当前阶段只生成和校验待审阅操作，不自动应用 AI 修改。

#### 2026-05-01 UI 与操作应用增量实现

- `Component\plugin\YDAgentServerWorker.lua`
  - 新增 RPC 方法 `ai.apply_plan(plan, options)`。
  - 默认 `dry_run=true`，只返回待执行操作。
  - 真正应用必须显式传入 `dry_run=false` 与 `confirm=true`。
  - 非 dry-run 应用还必须消费一次 UI 授权 token，否则返回 `UI approval is required before applying operations`。
  - 触发器操作映射到现有 `agent.set_*`、`agent.add_eca`、`agent.remove_eca`。
  - `object_set_field` 支持读取对象 JSON、修改字段、再写回对象文件；需要 `options.map_path`。
- `Component\plugin\YDAgentUI.lua`
  - 新增编辑器侧最小 UI 帮助模块。
  - 提供 Agent 状态、可用安全操作列表、应用流程说明。
  - 用户确认后通过 `bee.thread` 通道发放一次性 apply approval token。
- `Component\script\ydwe\ydwe_on_menu.lua`
  - 新增 `YDWE AI Agent` 菜单。
  - 包含 `Agent Status`、`Safe Operations`、`Generate Panel`、`Review Panel`、`Apply Workflow` 五个入口。

#### 2026-05-01 Review Panel / AI 生成 / 上下文压缩增量实现

- `Component\plugin\YDAgentServerWorker.lua`
  - 新增 `agent.compress_context(options)`，按 `trigger_limit` / `node_limit` 压缩多触发器上下文。
  - 新增 `agent.build_batch_prompt(instruction, options)`，生成多触发器 AI 操作计划提示。
  - 新增 `ai.generate_plan(prompt, context, options)`，调用当前 provider 并解析为安全操作计划。
  - 新增 `ai.generate_trigger_plan(index, instruction, options)`，对单个触发器生成操作计划。
  - 新增 `ai.generate_batch_plan(instruction, context_options, options)`，对批量触发器上下文生成操作计划。
  - 新增 `ai.generate_relationship_plan(instruction, context_options, options)`，基于关联分析生成可审阅操作计划。
  - 新增 `ai.queue_review(plan)`，允许外部 AI 客户端把已解析计划推送到编辑器 Review Panel。
- `Component\plugin\YDAgentAI.lua`
  - 新增 `generate_plan`，封装 `complete(..., { parse = true })` 并要求返回可校验计划。
- `Component\plugin\YDAgentUI.lua`
  - 新增 `show_review_panel`，显示最近 queued review plan。
  - Review Panel 支持刷新和 `Approve Next Apply` 一次性授权。
- `Component\script\ydwe\ydwe_on_menu.lua`
  - `YDWE AI Agent` 菜单新增 `Review Panel`。

#### 2026-05-01 交互式生成 / 代码生成 / 关联分析增量实现

- `Component\plugin\YDAgentServerWorker.lua`
  - 新增 `agent.analyze_relationships(options)`，基于 ECA 函数名和参数提取共享标识符，帮助发现变量/函数耦合。
  - 新增 `agent.build_relationship_prompt(instruction, options)`，把关联分析结果打包为 AI 提示。
  - 新增 `ai.generate_text(prompt, context, options)`，返回 provider 原始文本生成结果。
  - 新增 `ai.generate_code(language, instruction, context, options)`，用于 Jass/Lua 文本生成。
  - 新增 `ai.explain_trigger(index, options)`，对单个 GUI 触发器生成中文解释。
- `Component\plugin\YDAgentAI.lua`
  - 新增 `generate_text`，作为非操作计划类 AI 生成的统一入口。
- `Component\plugin\YDAgentUI.lua`
  - 新增 `show_generate_panel`，提供编辑器内交互式生成窗口。
  - Generate Panel 支持 `Generate Trigger Plan`、`Generate Batch Plan`、`Relation Plan`、`Explain Trigger`、`Analyze Relations`、`Generate JASS`。
  - UI 内置轻量 JSON-RPC client，直接调用 `127.0.0.1:27118` 本地 Agent 服务。
- `Component\script\ydwe\ydwe_on_menu.lua`
  - `YDWE AI Agent` 菜单新增 `Generate Panel`。

## 技术栈建议（已采用）

| 组件 | 已采用方案 | 备选方案 |
|------|-----------|---------|
| LSP Server | C++ (手写递归下降解析器) |  |
| 解析器 | 手写递归下降 (jass_lexer + jass_parser) | Tree-sitter |
| LSP 通信 | stdin/stdout JSON-RPC | WebSocket |
| AI API | Claude / OpenAI / 本地 LLM 请求构建、响应解析、安全应用、操作计划生成与代码生成入口 | 更完整的富文本结果面板待完成 |

## API 密钥管理

```cpp
// 配置存储
class AIConfig {
public:
    std::string apiKey;      // 加密存储
    std::string baseUrl;     // 自定义 API 端点
    std::string model;       // 模型选择
    int maxTokens = 2048;
    float temperature = 0.7f;
    
    // 本地路径 (用于本地LLM)
    std::string localModelPath;
};
```

## 隐私考虑

1. **代码不上云**: 敏感代码使用本地 LLM
2. **差分上传**: 只上传必要的上下文，而非完整代码
3. **用户控制**: 明确提示用户哪些数据会发送到云端

## 示例交互

### 代码生成
```
用户输入: "创建一个技能，对周围敌人造成伤害"

AI 生成:
```jass
function Trig_DamageArea_Actions takes nothing returns nothing
    local unit caster = GetTriggerUnit()
    local real x = GetUnitX(caster)
    local real y = GetUnitY(caster)
    local group g = CreateGroup()
    
    call GroupEnumUnitsInRange(g, x, y, 500.0, null)
    
    loop
        set bj_forLoopAIndex = FirstOfGroup(g)
        exitwhen bj_forLoopAIndex == null
        
        if IsUnitEnemy(bj_forLoopAIndex, GetOwningPlayer(caster)) then
            call UnitDamageTarget(caster, bj_forLoopAIndex, 100.0, ...)
        endif
        
        call GroupRemoveUnit(g, bj_forLoopAIndex)
    endloop
    
    call DestroyGroup(g)
    set caster = null
endfunction
```

### 错误检测
```
代码:
function Test takes nothing returns nothing
    local unit u = CreateUnit(...)
    // 忘记清理 u = null
endfunction

AI 提示:
⚠️ 内存泄漏警告: 局部变量 'u' 未被置空 (u = null)
建议在函数结束前添加 'set u = null'
```

## 下一步行动

1. ✅ LSP 服务器已实现 (C++, JSON-RPC over stdin/stdout)
2. ✅ 最小可行原型已完成 (代码补全、诊断、跳转定义)
3. ✅ Agent RPC 字段映射与 AI provider 配置层已实现
4. ✅ AI 响应解析、安全操作校验、触发器提示构建已实现
5. ✅ 最小编辑器 UI 集成与用户确认流程已实现
6. ✅ 安全操作应用层 dry-run / confirm 流程已实现
7. ✅ Review Panel、AI 操作计划生成入口、多触发器上下文压缩已实现
8. ✅ 交互式生成面板、AI 代码生成入口、触发器解释、多触发器关联分析已实现
9. 实现完整触发器 ↔ 自然语言转换器
10. 评估 Claude / OpenAI / 本地 LLM 成本与效果

## 后续推进参考（2026-05-01）

### 当前完成度判断

- 如果目标是“可演示、可人工确认、AI 能生成安全操作计划并通过 UI 审阅/应用”，当前约完成 **70% - 80%**。
- 如果目标是“稳定交付给普通地图作者长期使用的完整 AI 助手”，当前约完成 **50% - 60%**。

当前核心架构、安全边界、UI 入口、AI 生成/审阅/应用闭环已经初步成型。后续重点从“能力打通”转向“真实运行验证、UI 体验、转换语义、安全回滚、测试诊断”。

### 剩余主要工作

1. **运行时验证和修 bug**
   - 验证菜单是否正常出现。
   - 验证 JSON-RPC server 是否稳定监听 `127.0.0.1:27118`。
   - 验证 UI 内置 RPC client 是否能连接 Agent。
   - 验证 Generate Panel / Review Panel / apply token / `ai.apply_plan` 全链路。
   - 预计需要 1 - 3 轮调试修改。

2. **完整触发器 ↔ 自然语言转换器**
   - 触发器转自然语言：中文解释模板、本地化 ECA 名称、参数含义、风险识别。
   - 自然语言转触发器：常见触发器模板、结构化 ECA 操作生成、更强校验。
   - 预计需要新增 prompt/template 模块，约 300 - 800 行级别。

3. **Review Panel 产品化**
   - 从 JSON 原文展示升级为 operation 列表。
   - 展示 risk、warnings、errors、目标触发器/物编字段、应用前后差异。
   - 支持复制结果、部分批准、拒绝高风险操作、查看 dry-run、应用后刷新。
   - 主要修改 `YDAgentUI.lua`，约 300 - 700 行。

4. **操作应用层安全和回滚**
   - 应用前 snapshot。
   - 物编写回前 backup。
   - 操作失败时停止并返回已应用列表。
   - 更严格校验 ECA index、物编字段类型、禁止默认修改 original 记录。
   - 主要修改 `YDAgentOps.lua` 与 `YDAgentServerWorker.lua`。

5. **AI provider 配置体验**
   - UI 中配置 provider、model、endpoint。
   - 显示 API key 环境变量状态，不明文保存 key。
   - 支持 dry-run 请求预览。
   - 可新增 `YDAgentConfig.lua` 或继续扩展 `YDAgentUI.lua`。

6. **测试脚本和诊断工具**
   - 外部 JSON-RPC smoke test：`agent.list_triggers`、`agent.summarize_trigger`、`ai.validate_plan`、`ai.apply_plan` dry-run、`ai.queue_review`。
   - 编辑器内诊断：server alive、provider status、最近错误、通道状态。
   - 避免使用此前会卡住的 Debug `lua.exe` 语法检查流程。

### 推荐推进顺序

1. 补 smoke test / 诊断 RPC。
2. 增强 Review Panel：结构化显示 operations / warnings / errors。
3. 补 provider 配置 UI。
4. 补操作前 snapshot / backup / 更强校验。
5. 实现触发器自然语言解释模板。
6. 实现自然语言到常见触发器模板。
7. 继续扩展物编 AI 批量修改体验。

### 预估剩余修改规模

- **可演示 MVP**
  - 约 5 - 8 个文件。
  - 约 800 - 1500 行。
  - 重点是运行验证、UI/RPC 修正、Review Panel 列表化、provider 配置入口、smoke test。

- **较完整可用版本**
  - 约 8 - 15 个文件。
  - 约 2000 - 4000 行。
  - 重点是完整转换器、prompt 模板、回滚/备份、物编字段类型校验、结果面板、更多测试。

### 最大风险点

- **真实 WE 运行环境验证**：UI / socket / bee.thread / yue.gui 必须在真实编辑器内验证。
- **ECA 修改语义**：自然语言生成完整触发器需要更深入理解 TriggerData、ECA 模板和参数类型。
- **物编写回安全**：字段类型、字段来源、original 记录保护和备份仍需加强。

---

*设计文档更新: 2026-05-01 (Agent 字段映射、AI 接口配置层、响应解析、安全操作校验、UI、操作应用、Review Panel、AI 操作计划生成、代码生成入口、关联分析与后续推进路线图已落地)*
