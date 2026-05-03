# YDWE Refactor DeepSeek V4 Handoff

Last updated: 2026-05-02

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
- `Development\AI\ydagent_smoke.py` is now available for runtime smoke checks;
  `--restore` performs reversible trigger/global mutation.

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

### Unit Test Gate

```powershell
rtk Build\bin\Debug\test\YDWE_Test.exe
```

Expected result:

- `All tests passed`
- Current known count: 18 test cases, 169 assertions.

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
- `agent.list_globals` should return real global variable names and declaration-derived types after a save/compile path runs; value read and mutation remain follow-up work.

2026-05-03 result:

- `YDAgentServer` worker startup was fixed by preserving the worker thread handle and injecting `Development\Component\plugin\?.lua` into the worker `package.path`.
- `127.0.0.1:27118` listened successfully in real YDWE.
- `diag.status` and `diag.smoke` passed.
- After save triggered compilation, `agent.list_triggers` returned 5 real triggers from the demo map.
- `agent.get_eca_tree 0` read real event/action data.
- Reversible trigger rename passed: `对战初始化` -> `对战初始化__YDAGENT_SMOKE__` -> `对战初始化`.
- `agent.list_globals` returned 17 real global variable names after `GetGlobalVarName_Hook` captured names during save/compile, including `udg_unit`, `udg_lv`, `udg_angle`, `udg_RunIndex`, and `gg_trg_round`.
- `YDAgentServerWorker.lua` merges declaration-derived `type`, `type_name`, and `array` from `Development\Component\logs\currentmapscript.j`; real GUI validation distinguished `integer`, `real`, `unit`, and `trigger`.
- Global variable value read and `agent.set_global_value` remain follow-up work.

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

Only run this when `agent.list_globals` returns at least one global variable with
a safe string-like value. As of 2026-05-03, real GUI validation covers global
variable name/type enumeration only; value read and mutation are not yet verified.

```powershell
rtk python Development\AI\ydagent_client.py rpc agent.list_globals
rtk python Development\AI\ydagent_client.py rpc agent.set_global_value 0 "\"AI_Global_Smoke_20260502\""
rtk python Development\AI\ydagent_client.py rpc agent.global_value 0
```

Pass condition:

- `agent.set_global_value` returns `True`.
- `agent.global_value 0` returns the new value.

Then restore the original value immediately. If the target global type is not a
string-compatible field, pick another global or add a type-aware test first.

## Next Work Items

### P0: Real WorldEdit Runtime Verification

Goal: prove the Agent can mutate real GUI trigger and global variable state in
WorldEdit, not only through a stub.

Tasks:

- Launch `YDWE.exe` with a known demo map.
- Verify `YDAgentServer` starts and listens on `127.0.0.1:27118`.
- Run `diag.status`, `diag.smoke`, `agent.refresh`, `agent.list_triggers`,
  `agent.list_globals`.
- Perform reversible trigger rename.
- Perform reversible global variable value change.
- Capture logs from `Development\Component\logs\ydwe.log`.

Acceptance:

- Trigger rename is visible through RPC after mutation. Passed on 2026-05-03.
- Global value mutation is visible through RPC after mutation. Pending because real GUI validation currently covers global variable name/type enumeration only.
- No crash in `YDWE.exe`.
- Logs contain no Agent worker startup error.

### P1: Make Real Runtime Smoke Fully Scriptable

Goal: avoid human GUI steps.

Status (2026-05-02): core script delivered at
`Development\AI\ydagent_smoke.py`; real WorldEdit session run remains required.

Tasks:

- Add `Development\AI\ydagent_smoke.py`.
- It should poll TCP availability, run diagnostics, snapshot first trigger/global,
  mutate, verify, and restore.
- It should exit non-zero on failure and print a concise report.
- It must refuse destructive operations when no trigger/global exists.

Acceptance:

- `rtk python Development\AI\ydagent_smoke.py --restore` can validate a live
  WorldEdit session without manual clicks.
- The script leaves trigger/global state restored.

### P2: Harden Global Variable API

Goal: make global variable read/write type-aware and less offset-fragile.

Tasks:

- Identify exact global variable structure offsets for name/type/value.
- Validate `GetGlobalVarName` hook captures the correct container in real maps.
- Make `ydt_set_global_value` reject incompatible types or apply type-aware
  encoding.
- Add C++ tests where possible, and runtime smoke coverage through
  `ydagent_smoke.py`.

Acceptance:

- `agent.list_globals` returns stable names/types/values for at least two demo
  maps with globals.
- Incorrect type writes return `False` or a JSON-RPC error, not a crash.

### P3: Review Panel Productization

Goal: make AI plans inspectable and safer for map authors.

Tasks:

- Replace raw JSON display with operation rows.
- Show warnings, errors, target trigger/global/object field, and dry-run result.
- Add copy, refresh, approve-next-apply, and reject controls.
- Keep one-shot apply approval semantics.

Acceptance:

- A queued plan can be inspected in the editor without reading raw JSON.
- Dangerous operations are visibly marked.
- Apply still requires explicit review token.

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
