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
- `ydagent_client.py set_global_value` now distinguishes normal `.w3x` and LNI
  marker sessions
- `ydagent_client.py pending_globals` and `clear_pending_globals` are available
  for maintaining staged normal `.w3x` global overrides
- global parsing reads both `globals` and `InitGlobals`
- provider configuration UI exists in the editor
- AI apply flow supports snapshot/rollback

### Real Map Editing

Verified working:

- list real triggers
- rename real triggers
- list real globals
- create / modify / delete globals at map-file level
- in LNI marker sessions, enumerate globals directly from `trigger/variable.lml`
- in LNI marker sessions, write global values and read them back immediately
- in normal `.w3x` sessions, write scalar global defaults through the GUI WTG
  source, save/compile, reopen, and read back stable values
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

### 7. Pending Global Override Maintenance

Normal `.w3x` global writes are staged before the next save pipeline patches the
GUI WTG source. Inspect staged overrides:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py pending_globals
```

Clear all staged overrides after a deliberate manual GUI edit or after moving a
test map between validation runs:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py clear_pending_globals --all
```

Clear one map/global pair:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py clear_pending_globals Q:\path\to\map.w3x compose_count
```

## Verified Results

### Live Session

Verified in real YDWE sessions:

- trigger enumeration works
- trigger rename is reversible
- global names/types/values are readable
- LNI live session global write/readback works without native memory writes
- normal `.w3x` global write/readback works without native memory writes
- save/compile can be triggered from CLI
- object-editor and trigger-editor changes can be materialized into real maps

Concrete evidence from the 2026-05-09 validation pass:

- target session: `Q:\AppData\ydwe\work\compose_demo_counter_build\compose_demo_lni\.w3x`
- `diag.status` reported `global_count: 13` before any save
- `agent.global_name 10/11/12` resolved to:
  - `udg_compose_ready`
  - `udg_compose_stage`
  - `udg_compose_count`
- before write:
  - `agent.global_value 11 -> armed`
  - `agent.global_value 12 -> 0`
- live writes:
  - `set_global_value 12 7 -> VERIFIED`
  - `set_global_value 11 "armed_live" -> VERIFIED`
- backing file readback:
  - `agent.file_global_value udg_compose_count -> 7`
  - `agent.file_global_value udg_compose_stage -> armed_live`

Normal `.w3x` persistence evidence from the same validation pass:

- target session: `Q:\AppData\ydwe\work\compose_demo_gui_only_v2.w3x`
- after initial `save_map`, `diag.status` reported `global_count: 26`
- `agent.global_name 11/12` resolved to:
  - `udg_compose_stage`
  - `udg_compose_count`
- writes:
  - `set_global_value 12 13 -> VERIFIED`
  - `set_global_value 11 armed_persist -> VERIFIED`
- compiled script evidence:
  - `set udg_compose_stage="armed_persist"`
  - `set udg_compose_count=13`
- hard persistence check:
  - moved `ydagent_pending_globals.lua` aside
  - closed and reopened the `.w3x`
  - ran `save_map`
  - read back `agent.global_value 11 -> "armed_persist"`
  - read back `agent.global_value 12 -> 13`

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

- the old no-op `ydt_set_global_value` native export has been removed
- direct runtime memory writes for globals are not re-enabled
- current safe path depends on session type:
  - normal `.w3x`: patch pending scalar defaults into `war3map.wtg` during the
    save pipeline, compile, then read back through RPC
  - LNI marker map: edit `trigger/variable.lml` and read back through the live
    session RPC fallback

### Arrays

- array-global writes are not supported

### Automation Constraints

- editor automation is environment-sensitive
- tests must start from `YDWE.exe`
- generated logs and scratch files must be cleaned after testing
- LNI marker-map temp scripts now sanitize control bytes before Wave compile
  - this specifically masks the bad `W2L\x01` marker-name leak seen in some
    reopened `.w3xTemp\war3map.j` files
  - runtime restart is still required for an already-open editor process to pick
    up compiler-script changes on disk
- Agent `save_map` intentionally no-ops on LNI marker maps
  - YDWE's native GUI save path rewrites the LNI source directory from a temp
    marker map and can remove source-backed files such as `trigger/variable.lml`
  - use `set_global_value` directly for LNI marker globals

## Next-Phase Goals

### P0

- current active target: harden safe global writeback into an operator-friendly
  feature, not just a proof
- pending override management is now exposed through CLI; next validation should
  cover both all-clear and single-global clear against a real pending sidecar
- expand scalar global write coverage beyond integer and string
- keep direct native memory writes disabled unless a proven safe native path
  exists

### P1

- harden fresh-session verification so normal `.w3x` and LNI marker writeback can
  be revalidated automatically
- keep GUI-trigger-first test cases as the primary proof path

### P2

- extend real edit coverage with more object/trigger/global scenarios
- keep `docs/refactor-status.md` as the only status document

## Documentation Rule

When the refactor state changes:

- update this file
- update `README.md` only if the user-facing capability summary changed
- do not add new report-style markdown files
