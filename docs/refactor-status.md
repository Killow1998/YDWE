# YDWE Refactor Status

Last updated: 2026-05-09

This is the single source of truth for the current `ydwe-refactor` state.
Do not create new status, summary, handoff, or report markdown files elsewhere
in the repo. Update this file instead.

## Current State

### Repository

- active branch: `master`
- tracked push target: `killow/master`
- upstream source remote also exists as `origin`

### Build Baseline

- Visual Studio 2022
- Win32 + `v143`
- C++20
- `Build/lua/make.lua` already targets the current toolset

### Runtime Baseline

Use:

```powershell
Q:\AppData\ydwe\YDWE\Build\publish\Debug\YDWE.exe
```

Do not directly start `worldedit.exe` for Agent/runtime validation. The correct
path is `YDWE.exe -> worldeditydwe.exe`.

## Delivered Capabilities

### Core Refactor

- repo builds on the current VS2022 toolchain
- `YDTrigger` debug build is healthy
- test targets are buildable

### Agent Runtime

- JSON-RPC worker is integrated in the editor runtime
- `editor.save_map` is implemented
- `ydagent_client.py save_map` is available
- global parsing reads both `globals` and `InitGlobals`
- provider configuration UI exists in the editor
- AI apply flow supports snapshot/rollback

### Real Map Editing

Verified working:

- list real triggers
- rename real triggers
- list real globals
- create / modify / delete globals at map-file level
- read / write object-editor fields
- save / compile from CLI through the live editor session

### GUI-First Proof

The refactor already proved that the Agent path edits real GUI map content:

- GUI-only item-combine formulas were created
- GUI-only item grant was created
- GUI-only compose counter increment was created
- no custom helper script is required for the final GUI-only demo behavior

Reference demo artifact:

- `Q:\AppData\ydwe\work\compose_demo_gui_only_v2.w3x`

Generation helper:

- `Development/AI/ydmap_compose_demo.py`

## Operation Manual

### 1. Build

```powershell
MSBuild YDWE.sln /t:Build /p:Configuration=Debug /p:Platform=Win32
```

### 2. Launch

```powershell
Q:\AppData\ydwe\YDWE\Build\publish\Debug\YDWE.exe
```

### 3. Basic CLI Checks

Status:

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

### 4. Populate Live Session Data

Cold sessions may start with empty trigger/global caches. Use:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py save_map
```

This is the standard way to trigger a real editor save/compile cycle from CLI.

### 5. Safe Validation Sequence

1. start exactly one `YDWE.exe` session
2. load a map
3. run `save_map`
4. query triggers/globals
5. make one reversible trigger change
6. save again if needed
7. close test processes and clean generated residue

### 6. No-GUI Loopback

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_tui.py stub --restore
```

Use this before GUI validation when changing RPC/runtime behavior.

## Verified Results

### Live Session

Verified in real YDWE sessions:

- trigger enumeration works
- trigger rename is reversible
- global names/types/values are readable
- save/compile can be triggered from CLI
- object-editor and trigger-editor changes can be materialized into real maps

### Example Validation Target

The example AI map under `Development\Component\example(...)\AI\` was used as a
verification target only. It should not be treated as an intended repo content
change.

### GUI-Only Compose Demo

Verified behavior of the final GUI-only compose demo:

- formulas live in GUI triggers
- test items are granted in GUI triggers
- compose count is tracked in a GUI global
- user-side manual test already succeeded

## Known Limits

### Native Global Write

- `ydt_set_global_value` is still a safe no-op in native code
- direct runtime memory writes for globals are not re-enabled
- current safe path is:
  - edit map-backed data
  - trigger save/compile
  - read back through RPC

### Arrays

- array-global writes are not supported

### Automation Constraints

- editor automation is environment-sensitive
- tests must start from `YDWE.exe`
- generated logs and scratch files must be cleaned after testing

## Next-Phase Goals

### P0

- design a safe native write path for `ydt_set_global_value`
- keep failure behavior fail-closed until that path is proven safe

### P1

- continue hardening scripted live-session verification
- keep GUI-trigger-first test cases as the primary proof path

### P2

- extend real edit coverage with more object/trigger/global scenarios
- keep `docs/refactor-status.md` as the only status document

## Documentation Rule

When the refactor state changes:

- update this file
- update `README.md` only if the user-facing capability summary changed
- do not add new report-style markdown files
