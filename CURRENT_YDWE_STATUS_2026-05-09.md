# YDWE Current Status 2026-05-09

## Repo / Git

- Repo root: `Q:\AppData\ydwe\YDWE`
- Current branch: `master`
- Current upstream tracking: `killow/master`

Current remotes:

- `killow` -> `git@github.com:Killow1998/YDWE.git`
- `origin` -> `https://github.com/actboy168/YDWE.git`

Practical meaning:

- `killow` is the branch's current push/fetch upstream.
- `origin` looks like the upstream source repository.

## Current Worktree

The worktree is **dirty**. It already contains staged-equivalent local changes from
this refactor session and earlier session work, including:

- Agent server / UI / worker code
- AI client and docs
- test updates
- example map changes
- generated scratch files such as `mem.txt`, `mem1.txt`, `mem_stride.txt`

Because the tree is not clean, pushing blindly is not recommended until the
final commit scope is chosen.

## Delivered Agent Capabilities

The local YDWE Agent execution chain is working end-to-end:

- real trigger listing
- real trigger mutation
- real global listing
- map-level global create / modify / delete
- object editor read / write
- CLI-triggered real save / compile via `editor.save_map`

Execution chain:

`YDWE.exe -> YDTrigger.dll -> YDAgentServerWorker.lua -> JSON-RPC -> ydagent_client.py`

## Verified Runtime Behavior

### 1. Trigger editing

Verified in live YDWE sessions:

- trigger enumeration works
- reversible trigger rename works
- added GUI triggers are visible in the editor

### 2. Global variables

Verified:

- global names and types are readable
- `InitGlobals` values are now read back correctly
- example: `udg_compose_stage = "armed"` can be read correctly from live RPC

Important limitation:

- `ydt_set_global_value` in C++ is still intentionally disabled
- runtime in-memory direct global writes are **not** restored
- safe path is still: map-level edit -> save/compile -> read back

### 3. Object editor

Verified:

- custom item `[I003]`
- display name: `合成神符`

### 4. Save / compile

Verified:

- `rtk python Development\AI\ydagent_client.py save_map`
- cold start may show `trigger_count=0` / `global_count=0`
- after real save/compile, live trigger/global data becomes available

## Demo Maps Produced

### GUI-only validated demo map

Primary recommended map for inspection and manual testing:

- `Q:\AppData\ydwe\work\compose_demo_gui_only_v2.w3x`

This version is important because:

- item-combine formula registration is in GUI triggers
- item grant is in GUI triggers
- compose counter increment is in GUI triggers
- no custom injected `ItemComposeSmoke` helper remains in `trigger/code.j`

Key GUI trigger files in the verified unpacked tree:

- `Q:\AppData\ydwe\work\compose_demo_gui_only_v2_build\compose_demo_verify_lni\trigger\2-物品合成测试\1-DefinedFormula.lml`
- `Q:\AppData\ydwe\work\compose_demo_gui_only_v2_build\compose_demo_verify_lni\trigger\2-物品合成测试\2-合成事件.lml`
- `Q:\AppData\ydwe\work\compose_demo_gui_only_v2_build\compose_demo_verify_lni\trigger\2-物品合成测试\3-发放测试物品.lml`
- `Q:\AppData\ydwe\work\compose_demo_gui_only_v2_build\compose_demo_verify_lni\trigger\variable.lml`

GUI trigger folder structure:

- `DefinedFormula`
- `合成事件`
- `发放测试物品`

### Demo behavior

The GUI-only map includes:

- formula 1: `rat6 + rat9 + I003 -> ratc`
- formula 2: `rde1 + rde2 + I003 -> rde3`
- formula 3: `rhth x6 -> mcou`

Global variables:

- `compose_ready`
- `compose_stage`
- `compose_count`

Current GUI-only item grant behavior:

- directly gives the first two test sets to `gg_unit_Hpal_0032`
- drops 6 `rhth` near `gg_unit_Hpal_0032`
- compose counter increments in GUI with:
  - `SetVariable`
  - `OperatorIntegerAdd`

## Key Files Changed In This Phase

Core runtime / agent:

- `Build\publish\Debug\plugin\YDAgentServerWorker.lua`
- `Development\Component\plugin\YDAgentServerWorker.lua`
- `Development\Build\bin\Debug\plugin\YDAgentServerWorker.lua`
- `Development\AI\ydagent_client.py`

Documentation:

- `DEEPSEEKV4_REFACTOR_HANDOFF.md`
- `PROGRESS_SUMMARY.md`
- `REFACTORING_REPORT.md`
- `AI_FEATURE_DESIGN.md`
- `Development\AI\ydwe-lsp\README.md`

Demo generation:

- `Development\AI\ydmap_compose_demo.py`

## Remaining Gaps

1. Runtime direct global write is still disabled
   - current C++ `ydt_set_global_value(...)` remains a safe no-op

2. Worktree cleanup is still needed
   - scratch files and generated files are mixed with source edits
   - commit boundaries are not yet clean

3. GitHub upload decision is still pending
   - because there are two remotes
   - and the current tree includes more than one logical change set

## Recommended Next Step

Before pushing to GitHub:

1. decide the target remote:
   - `killow/master` for the current tracked branch
   - or `origin/master` if you intend to push to the upstream repo
2. split the current dirty tree into at least:
   - agent/runtime changes
   - docs changes
   - demo map / generated artifacts
3. exclude scratch files if they are only debugging residue:
   - `mem.txt`
   - `mem1.txt`
   - `mem_stride.txt`

## Minimal Operator Commands

Check agent status:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py status
```

List triggers:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py rpc agent.list_triggers
```

List globals:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py rpc agent.list_globals
```

Real save / compile:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py save_map
```
