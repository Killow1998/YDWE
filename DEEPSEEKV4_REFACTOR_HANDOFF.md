# YDWE Refactor DeepSeek V4 Handoff

Last updated: 2026-05-08

This document is the execution guide for a DeepSeek V4 based agent to continue
`ydwe-refactor`. It is intentionally operational: follow the task order, run the
verification gates, and do not rely on manual testing unless a gate explicitly
requires the real WorldEdit GUI.

## Operating Rules

- Work from `Q:\AppData\ydwe\YDWE`.
- Prefix shell commands with `rtk`, per `Q:\AppData\ydwe\RTK.md`.
- Do not revert unrelated dirty files or build artifacts.
- Prefer narrow patches. Keep third-party code changes isolated.
- Build with Visual Studio 2022 MSBuild:
  `Q:\Apps\VisualStudioProfessional2022\MSBuild\Current\Bin\MSBuild.exe`.
- The current repo may contain generated binaries under `Build/` and
  `Development/Component/bin/`; do not treat them as source-of-truth.

## Current State

The modernization baseline is in place:

- VS2022 / v143 / C++20 project upgrade is complete.
- Core utility modernization is partially complete (`noncopyable`,
  `singleton`, `horrible_cast`, hook memory ownership).
- YDWE LSP exists for Jass/Lua text editing.
- YDTrigger Agent API exports 25 C functions:
  triggers, ECA, object editor read/write, trigger create/delete, and global
  variable read/write.
- JSON-RPC TCP worker is expected at `127.0.0.1:27118`.
- File IPC fallback remains in `Development\Component\plugin\YDAgentServer.lua`.
- `diag.status`, `diag.smoke`, and `agent.list_globals` loopback were verified
  with a test stub on 2026-05-02.
- `ai.apply_plan` dry-run returns per-operation `preview` snapshots; non-dry-run
  results include read-back `after` / `verified` fields where RPC can verify the
  operation.
- `Development\AI\ydagent_smoke.py` is now available for runtime smoke checks;
  `--restore` performs reversible trigger mutation. Global variable mutation is
  intentionally disabled until a real reversible storage path is identified.
- `Development\AI\ydagent_tui.py` is the preferred no-GUI self-test harness.
  `stub --restore` starts `YDAgentServerWorker.lua` with `YDAGENT_TEST_STUB`,
  runs JSON-RPC/AI dry-run/global-write-rejection checks, verifies reversible
  trigger rename, and exits without user interaction.
- `Development\AI\ydagent_client.py save_map` now triggers a real editor save
  through `editor.save_map`, waits for the worker to reconnect, and is the
  preferred way to populate trigger/global caches in a live session.
- `YDAgentDump` auto-save smoke hooks are disabled in all active plugin config
  roots because they destabilized live save verification.

Known source files for the Agent path:

- `Development\Plugin\WE\YDTrigger\AgentAPI.cpp`
- `Development\Plugin\WE\YDTrigger\AgentAPI.h`
- `Development\Plugin\WE\YDTrigger\Common.cpp`
- `Development\Component\plugin\YDAgentCore.lua`
- `Development\Component\plugin\YDAgentServer.lua`
- `Development\Component\plugin\YDAgentServerWorker.lua`
- `Development\Component\plugin\YDAgentAI.lua`
- `Development\Component\plugin\YDAgentOps.lua`
- `Development\AI\ydagent_client.py`

## Verification Gates

Run these after every code change touching Agent, YDTrigger, LSP, Lua plugin, or
object editor behavior.

### Build Gates

```powershell
rtk "Q:\Apps\VisualStudioProfessional2022\MSBuild\Current\Bin\MSBuild.exe" Development\Plugin\WE\YDTrigger\YDTrigger.vcxproj /p:Configuration=Debug /p:Platform=Win32 /m
rtk "Q:\Apps\VisualStudioProfessional2022\MSBuild\Current\Bin\MSBuild.exe" Development\Test\YDWE_Test.vcxproj /p:Configuration=Debug /p:Platform=Win32 /m
```

Expected result:

- `YDTrigger.vcxproj` succeeds.
- `YDWE_Test.vcxproj` succeeds.

### 2026-05-07 Progress Update (Current handoff)

- `AgentAPI.cpp` regression from prior refactor was traced to a local simplification pass that removed trigger/global implementation details (`ydt_add_eca`, `ydt_create_trigger`, `ydt_delete_trigger`) and changed export expectations.
- I restored `AgentAPI.cpp` back to the project HEAD-compatible implementation and removed the stray `YDTrigger.def` export of `ydt_mem_dump` to recover link health.
- Build now succeeds with local MSBuild:
  - `rtk "Q:\Apps\VisualStudioProfessional2022\MSBuild\Current\Bin\MSBuild.exe" Development\Plugin\WE\YDTrigger\YDTrigger.vcxproj /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v143 /m`
- Compiled DLL has been copied into `Development\Component\plugin\YDTrigger.dll` and `Build\publish\Debug\plugin\YDTrigger.dll`.

### 2026-05-08 Progress Update

- Real-session verification now starts from `Build\publish\Debug\YDWE.exe`;
  do not directly launch `worldedit.exe`. YDWE will spawn
  `worldeditydwe.exe` itself.
- `YDAgentServerWorker.lua` now exposes `editor.save_map()`. It finds the live
  editor window for the current process, resolves the real menu item caption
  (`保存地图(&S)` / `Save Map`), and dispatches `WM_COMMAND` instead of relying
  on blind key simulation.
- `Development\AI\ydagent_client.py save_map` calls `editor.save_map` and waits
  for `diag.status` to come back after the save/compile cycle.
- Real demo-map verification on
  `Development\Component\example(演示地图)\AI\AI——RPG佣兵AI.w3x` passed:
  before save, `trigger_count=0` and `global_count=0`; after scripted
  `save_map`, `trigger_count=4` and `global_count=20`.
- Real trigger mutation is verified on the same map:
  `begin -> begin__AI_SMOKE__ -> begin`.
- Real global read is verified on the same map. Example:
  `udg_i` (`index=3`, `type=integer`) reads back `0`.
- Real global write is still intentionally blocked. `agent.set_global_value`
  returns `false`, and `Development\Plugin\WE\YDTrigger\AgentAPI.cpp`
  currently implements `ydt_set_global_value(...) { return 0; }` to avoid the
  previously observed save corruption path.
- A second scripted `save_map` after the mutation tests completed without
  crashing the live session; `diag.status` still reported `trigger_count=4` and
  `global_count=20`.

### Current no-GUI verification

`YDTrigger` C++/RPC surface is healthy at the stub layer, and the real GUI path
has been verified on a live demo map. Keep the live checklist below as the
regression script for future changes.

```powershell
rtk python Development\AI\ydagent_tui.py stub --restore
```

Expected result (example):

```
=== YDWE Agent TUI - stub loopback ===
[PASS] diag.status
[PASS] diag.smoke
[PASS] agent.list_triggers
[PASS] agent.list_globals
[PASS] agent.compress_context
[PASS] ai.operation_schema
[PASS] ai.apply_plan dry-run
[PASS] agent.set_global_value rejects unsafe write
[PASS] agent.set_trigger_name mutate
[PASS] agent.trigger_name verify mutation
[PASS] agent.trigger_name verify restore
=== Summary: 11 passed, 0 failed ===
```

Current real-GUI checkpoint status:
1. open exactly one `YDWE.exe` session;
2. load a map that has GUI triggers and globals;
3. run `rtk python Development\AI\ydagent_client.py save_map`;
4. run:
   - `rtk python Development\AI\ydagent_client.py status`
   - `rtk python Development\AI\ydagent_client.py list_triggers`
   - `rtk python Development\AI\ydagent_client.py rpc agent.list_globals`
   - `rtk python Development\AI\ydagent_client.py set_trigger_name 0 "begin__AI_SMOKE__"`
   - `rtk python Development\AI\ydagent_client.py set_trigger_name 0 "begin"`
   - `rtk python Development\AI\ydagent_client.py set_global_value 3 123`
5. expected result:
   - trigger/global counts become non-zero after `save_map`;
   - trigger rename is reversible;
   - `set_global_value` currently returns `FAIL` until a safe write path exists.

### Unit Test Gate

```powershell
rtk Build\bin\Debug\test\YDWE_Test.exe
```

Expected result:

- `All tests passed`
- Current known count: 19 test cases, 184 assertions.

If the count changes because tests were added, update:

- `PROGRESS_SUMMARY.md`
- `REFACTORING_REPORT.md`
- `AI_FEATURE_DESIGN.md`
- `Development\AI\ydwe-lsp\README.md` if API coverage changed

### Lua/Python Syntax Gate

```powershell
rtk Development\Component\bin\lua.exe -e "assert(loadfile('Development/Component/plugin/YDAgentAI.lua')); assert(loadfile('Development/Component/plugin/YDAgentCore.lua')); assert(loadfile('Development/Component/plugin/YDAgentServer.lua')); assert(loadfile('Development/Component/plugin/YDAgentServerWorker.lua'))"
rtk python -m py_compile Development\AI\ydagent_client.py
```

Expected result: exit code 0.

## No-Human Loopback Testing

The real `YDTrigger.dll` installs WorldEdit hooks in `DllMain`, so do not load it
inside plain `lua.exe` for loopback tests. Use the test-stub entry in
`YDAgentServerWorker.lua` to validate TCP, JSON-RPC, dispatch, diagnostics, and
global variable RPC without opening WorldEdit.

Preferred one-command TUI/CLI gate:

```powershell
rtk python Development\AI\ydagent_tui.py stub --restore
```

Expected result:

- `diag.status`, `diag.smoke`, `agent.list_triggers`, `agent.list_globals`,
  `agent.compress_context`, `ai.operation_schema`, and `ai.apply_plan` dry-run
  pass.
- `agent.set_global_value` returns `false`, preserving the current safe boundary.
- Trigger rename/restore passes against the in-process Lua test stub.

Use the manual stub worker commands below only when debugging the worker itself.

### Start Stub Worker

Run this from `Q:\AppData\ydwe\YDWE`. It is expected to keep running.
If your tool has a timeout, let it time out after the service starts, then run
the client commands below.

```powershell
rtk Development\Component\bin\lua.exe -e "package.path='Q:/AppData/ydwe/YDWE/Development/Component/plugin/?.lua;'..package.path; package.cpath='Q:/AppData/ydwe/YDWE/Development/Component/bin/?.dll;'..package.cpath; _G.YDAGENT_COMPONENT_ROOT='Q:/AppData/ydwe/YDWE/Development/Component'; _G.YDAGENT_TEST_STUB={ydt_refresh=function() return 0 end, ydt_get_trigger_count=function() return 0 end, ydt_get_trigger_name=function() return nil end, ydt_get_trigger_disabled=function() return 0 end, ydt_get_eca_count=function() return 0 end, ydt_get_eca_func_name=function() return nil end, ydt_get_eca_gui_id=function() return -1 end, ydt_get_eca_param_count=function() return 0 end, ydt_get_eca_param_value=function() return nil end, ydt_set_trigger_name=function() return 1 end, ydt_set_trigger_disabled=function() return 1 end, ydt_set_eca_func_name=function() return 1 end, ydt_set_eca_active=function() return 1 end, ydt_set_eca_param_value=function() return 1 end, ydt_add_eca=function() return 1 end, ydt_remove_eca=function() return 1 end, ydt_create_trigger=function() return 1 end, ydt_delete_trigger=function() return 1 end, ydt_get_global_count=function() return 1 end, ydt_get_global_name=function() return 'udg_Test' end, ydt_get_global_type=function() return 3 end, ydt_get_global_value=function() return '42' end, ydt_set_global_value=function() return 1 end, ydt_read_object_file=function() return nil end, ydt_write_object_file=function() return 1 end}; require 'YDAgentServerWorker'"
```

### Run Loopback Clients

```powershell
rtk python Development\AI\ydagent_client.py status
rtk python Development\AI\ydagent_client.py smoke
rtk python Development\AI\ydagent_client.py rpc agent.list_globals
rtk python Development\AI\ydagent_client.py rpc agent.set_global_value 0 "\"43\""
rtk python Development\AI\ydagent_client.py rpc agent.create_trigger "\"AI_Loopback_Trigger\""
rtk python Development\AI\ydagent_client.py rpc agent.delete_trigger 0
```

Expected result:

- `status` returns `ok: True` and `port: 27118`.
- `smoke` returns `OK` for all checks.
- `agent.list_globals` returns at least one stub global, `udg_Test`.
- `agent.set_global_value` returns `True`.
- `agent.create_trigger` returns non-zero in the stub.
- `agent.delete_trigger` returns `True`.

### Stop Stub Worker

Find and stop the test `lua.exe` process:

```powershell
rtk powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"name = 'lua.exe'\" | Select-Object ProcessId,CommandLine"
rtk powershell -NoProfile -Command "Stop-Process -Id <PID> -Force"
```

Only stop the process whose command line contains `YDAgentServerWorker` and
`YDAGENT_TEST_STUB`.

## Real GUI Validation

Stub loopback proves the Agent server and RPC dispatch. It does not prove
WorldEdit memory structure mutation. Real mutation needs YDWE/WorldEdit running
with a loaded map.

Suggested map for smoke testing:

```text
Development\Component\example(演示地图)\系统\中心计时器-单位环绕(全局变量版).w3x
```

Manual GUI is not required after the map is loaded. Use RPC for verification.

### Launch GUI

```powershell
rtk powershell -NoProfile -Command "Start-Process -FilePath 'Q:\AppData\ydwe\YDWE\Development\Component\YDWE.exe' -WorkingDirectory 'Q:\AppData\ydwe\YDWE\Development\Component' -ArgumentList '-loadfile','Q:\AppData\ydwe\YDWE\Development\Component\example(演示地图)\系统\中心计时器-单位环绕(全局变量版).w3x'"
```

Then poll:

```powershell
rtk python Development\AI\ydagent_client.py status
rtk python Development\AI\ydagent_client.py smoke
rtk python Development\AI\ydagent_client.py refresh
rtk python Development\AI\ydagent_client.py list_triggers
rtk python Development\AI\ydagent_client.py rpc agent.list_globals
```

Expected result:

- TCP connects.
- `diag.status` and `diag.smoke` pass.
- Trigger data may remain empty until a save/compile path runs; after compilation reaches `CC_PutTrigger_Hook`, `agent.list_triggers` should return real triggers.
- `agent.list_globals` should return real global variable names, declaration-derived types, array flags, and scalar declaration initial values after a save/compile path runs. Runtime storage value read and mutation remain follow-up work.

2026-05-03 result:

- `YDAgentServer` worker startup was fixed by preserving the worker thread handle and injecting `Development\Component\plugin\?.lua` into the worker `package.path`.
- `127.0.0.1:27118` listened successfully in real YDWE.
- `diag.status` and `diag.smoke` passed.
- After save triggered compilation, `agent.list_triggers` returned 5 real triggers from the demo map.
- `agent.get_eca_tree 0` read real event/action data.
- Reversible trigger rename passed: `对战初始化` -> `对战初始化__YDAGENT_SMOKE__` -> `对战初始化`.
- `agent.list_globals` returned 17 real global variable names after `GetGlobalVarName_Hook` captured names during save/compile, including `udg_unit`, `udg_lv`, `udg_angle`, `udg_RunIndex`, and `gg_trg_round`.
- `YDAgentServerWorker.lua` merges declaration-derived `type`, `type_name`, and `array` from `Development\Component\logs\currentmapscript.j`; real GUI validation distinguished `integer`, `real`, `unit`, and `trigger`.
- `agent.global_value` returns declaration initial values from `currentmapscript.j` for declared globals; validated `udg_RunIndex=0`, `udg_data=0`, `gg_trg_round=null`, and array globals such as `udg_unit` return `null`.
- Disassembly/probes identified the name layout used by `GetGlobalVarName`: `This+0x08` is count and `This+0x0C + index * 0x1C0 + 0x2E` is the name field. `0x005C6840` reads WE GUI display/parse text, not a safe runtime value/write path.
- Runtime storage value read and `agent.set_global_value` remain follow-up work; declared globals currently return `false` for mutation.

### Trigger Mutation Check

Use a reversible rename:

```powershell
rtk python Development\AI\ydagent_client.py rpc agent.trigger_name 0
rtk python Development\AI\ydagent_client.py set_trigger_name 0 "AI_Rename_Smoke_20260502"
rtk python Development\AI\ydagent_client.py rpc agent.trigger_name 0
```

Pass condition:

- The second `agent.trigger_name 0` returns `AI_Rename_Smoke_20260502`.

Then restore the original name immediately.

### Global Variable Mutation Check

Do not run this as a pass/fail mutation gate yet. As of 2026-05-05, real GUI
validation covers global variable name/type/array flags and declaration initial
values only. Runtime storage value read and mutation are not yet verified, and
declared globals should return `false` for mutation attempts.

```powershell
rtk python Development\AI\ydagent_client.py rpc agent.list_globals
rtk python Development\AI\ydagent_client.py rpc agent.set_global_value 0 "\"AI_Global_Smoke_20260502\""
rtk python Development\AI\ydagent_client.py rpc agent.global_value 0
```

Current safe pass condition:

- `agent.set_global_value` returns `false` for declared real globals.
- `agent.global_value` continues to return the declaration initial value.

## Next Work Items

### P0: Real WorldEdit Runtime Verification

Goal: prove the Agent can read real GUI trigger/global state and safely mutate
supported trigger state in WorldEdit, not only through a stub.

Tasks:

- Launch `YDWE.exe` with a known demo map.
- Verify `YDAgentServer` starts and listens on `127.0.0.1:27118`.
- Run `diag.status`, `diag.smoke`, `agent.refresh`, `agent.list_triggers`,
  `agent.list_globals`.
- Perform reversible trigger rename.
- Verify global variables expose names, types, array flags, scalar declaration
  initial values, and return `false` for unsafe mutation attempts.
- Capture logs from `Development\Component\logs\ydwe.log`.

Acceptance:

- Trigger rename is visible through RPC after mutation. Passed on 2026-05-03.
- Global variable name/type/array/declaration initial value read is visible
  through RPC. Passed on 2026-05-05.
- Global value mutation remains pending until a real reversible storage path is
  identified.
- No crash in `YDWE.exe`.
- Logs contain no Agent worker startup error.

### P1: Make Real Runtime Smoke Fully Scriptable

Goal: avoid human GUI steps.

Status (2026-05-05): core script delivered at
`Development\AI\ydagent_smoke.py`; `--restore` verifies trigger rename and
restore only. `Development\AI\ydagent_tui.py stub --restore` now provides the
default no-GUI loopback gate. Real WorldEdit session run remains required only
for C++ hook and real memory layout validation.

Tasks:

- Add `Development\AI\ydagent_smoke.py`.
- Add `Development\AI\ydagent_tui.py`.
- It should poll TCP availability, run diagnostics, snapshot a trigger, mutate,
  verify, and restore the trigger name.
- It should exit non-zero on failure and print a concise report.
- It must refuse destructive operations when no trigger exists.

Acceptance:

- `rtk python Development\AI\ydagent_smoke.py --restore` can validate a live
  WorldEdit session without manual clicks.
- `rtk python Development\AI\ydagent_tui.py stub --restore` validates the Agent
  RPC loop without opening WorldEdit.
- The script leaves trigger state restored.

### P2: Harden Global Variable API

Goal: make global variable read/write type-aware and less offset-fragile.

Tasks:

- Identify exact global variable structure offsets for name/type/value. (Status: Offset for string value identified at `varray + index * 0x1C0 + 0x92`. Name offset confirmed, type offset confirmed).
- Validate `GetGlobalVarName` hook captures the correct container in real maps. (Passed).
- Make `ydt_set_global_value` reject incompatible types or apply type-aware
  encoding. (Status: Currently reverted to a safe stub returning 0. Raw `BLZSStrCopy` to `0x92` corrupts trigger structures and breaks map saving. Needs a safer mutation method, perhaps calling internal WE functions).
- Add C++ tests where possible, and runtime smoke coverage through
  `ydagent_smoke.py`.

Acceptance:

- `agent.list_globals` returns stable names/types/values for at least two demo
  maps with globals. (Status: Reading is stable using `0x92` offset, but `YDAgentServerWorker.lua` may still return `initial_value` if cached).
- Incorrect type writes return `False` or a JSON-RPC error, not a crash. (Status: Safe stub prevents crashes, but actual writes are disabled).

### P3: Review Panel Productization

Goal: make AI plans inspectable and safer for map authors.

Tasks:

- Replace raw JSON display with operation rows. Done for queued plans; Raw JSON
  remains available at the bottom for debugging.
- Show warnings, errors, target trigger/global/object field, and dry-run result.
  Done for validation warnings/errors, cleaned operations, and dry-run preview
  snapshots.
- Add copy, refresh, approve-next-apply, and reject controls.
- Keep one-shot apply approval semantics.
- Add AI Provider Configuration UI to configure models, API keys, and endpoints, including support for CLI Coding Agents (gemini, claude, codex, copilot). Done.
- Implement operation-level apply rollback using pre-operation snapshots. Done.

Acceptance:

- A queued plan can be inspected in the editor without reading raw JSON. (Passed)
- Dangerous operations are visibly marked. (Passed)
- Apply still requires explicit review token. (Passed)
- Applying an operation that fails will safely roll back previously applied operations. (Passed)
- Providers can be configured via a GUI panel. (Passed)

### P4: Natural Language Conversion

Goal: build full trigger-to-natural-language and natural-language-to-trigger
workflow.

Tasks:

- Add localized ECA explanation templates.
- Convert trigger summaries into Chinese explanations with risk notes.
- Add common trigger templates for natural-language generation.
- Keep all generation output as reviewable operation plans, not direct writes.

Acceptance:

- `ai.explain_trigger` produces useful Chinese explanations for representative
  demo maps.
- Natural-language generation creates a valid plan that passes
  `ai.validate_plan`.

## Documentation Sync Rules

When a task changes behavior, update the docs in the same patch:

- `PROGRESS_SUMMARY.md` for project status and verification numbers.
- `REFACTORING_REPORT.md` for roadmap status and validation records.
- `AI_FEATURE_DESIGN.md` for Agent architecture and newly landed methods.
- `Development\AI\ydwe-lsp\README.md` for external RPC methods and operator
  instructions.
- `Q:\AppData\ydwe\CLAUDE.md` for cross-session agent facts and known limits.

Never leave completed work listed as "待修复". Use absolute dates such as
`2026-05-02`.

## 2026-05-08 当前收口状态

本轮针对目标
`完成全局变量的准确写入，并完成物体编辑器和触发编辑器的修改，在地图中构造出一个简单的地图功能——物品合成和实现全局变量的创建、修改、删除算作成功`
做了补强和复验。

### 已落地代码

- `Build\publish\Debug\plugin\YDAgentServerWorker.lua`
  - `load_script_global_types()` 不再只读 `globals` 声明段，现已继续解析 `InitGlobals`，因此 `agent.global_value` / `agent.list_globals` 能准确回读类似 `udg_compose_stage="armed"` 这类初始化赋值。
  - 新增 `editor.current_map_path()`，用于从 `logs\ydwe.log` 推断当前地图路径；已补控制字符清洗。
  - 仍保留 `ydt_set_global_value` 的安全 no-op 策略；内存写全局值没有恢复。
- `Development\AI\ydagent_client.py`
  - 新增 `create_global` / `delete_global` CLI 入口。

### 真实会话验证

使用：

- `Q:\AppData\ydwe\YDWE\Build\publish\Debug\YDWE.exe`
- 地图：`Q:\AppData\ydwe\work\compose_demo_ascii.w3x`

验证结果：

- 冷启动后保存前：`trigger_count=0`、`global_count=0`
- `save_map` 后：`trigger_count=6`、`global_count=24`
- `agent.list_triggers` 返回：
  - 原图触发器：`begin`、`shezhi1`、`shezhi2`、`stop`
  - 新增合成触发器：`DefinedFormula`、`合成事件`
- `agent.list_globals` 返回：
  - `udg_compose_ready (integer) = 0`
  - `udg_compose_stage (string) = "armed"`
  - 不存在 `udg_compose_temp`
- 触发器可逆修改再次通过：
  - `DefinedFormula -> DefinedFormula__AI_SMOKE__ -> DefinedFormula`
- 再次 `save_map` 后会话稳定，未复现保存即崩溃。

### 文件级证据

- 全局变量 CRUD 结果：
  - `Q:\AppData\ydwe\work\compose_demo_build\compose_demo_verify_lni\trigger\variable.lml`
  - 现有：
    - `compose_ready: integer`
    - `Def   : 0`
    - `compose_stage: string`
    - `Def   : armed`
  - 已删除：
    - `compose_temp`
- 物编结果：
  - `Q:\AppData\ydwe\work\compose_demo_build\compose_demo_verify_lni\table\item.ini`
  - 已新增 `[I003]`
  - `Name = "合成神符"`
- 触发器结果：
  - `trigger\2-物品合成测试\1-DefinedFormula.lml`
  - `trigger\2-物品合成测试\2-合成事件.lml`
  - 关键调用：
    - `YDWENewItemsFormula`
    - `YDWESyStemItemCombineRegistTrigger`
- 编译脚本结果：
  - `Build\publish\Debug\logs\currentmapscript.j`
  - 可见：
    - `set udg_compose_stage="armed"`
    - `call YDWENewItemsFormula(... 'rat6', 'rat9', 'I003' ... 'ratc')`
    - `call ExecuteFunc("InitItemComposeSmoke")`

### 仍然成立的限制

- `ydt_set_global_value` 仍然是安全禁用状态，不能把“真实运行时内存写全局值”算作已解决。
- 试图直接把当前打开地图当成持久 LNI 工作区去改 `*.w3xTemp\trigger\variable.lml` 这条路当前不成立；该 temp 目录不是稳定常驻的可编辑 LNI 树。
- 当前已完成的是：
  - 地图级全局变量创建/修改/删除
  - 真实会话里的准确读回
  - 触发器编辑验证
  - 物编结果验证
  - 物品合成功能落地图并编译通过
