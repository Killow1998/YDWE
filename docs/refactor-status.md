# YDWE Refactor Status

Last updated: 2026-05-10

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

Direct debug startup with a bare map path is supported again:

```powershell
Q:\AppData\ydwe\YDWE\Build\publish\Debug\YDWE.exe Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x
```

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
- name-based global lookup/write is available through Agent RPC and CLI
- `ydagent_smoke.py --restore-global` is validated against a real normal `.w3x`
  session after extending slow RPC timeouts
- `ydagent_live_regression.py` can launch or attach to a debug YDWE session,
  verify the loaded map, run `save_map`, and perform a reversible scalar global
  writeback check
- live regression also covers reversible trigger rename and object-editor field
  metadata lookup
- `object.read` can now extract and parse real `.w3x` archive object files
  through the native ObjectAPI path
- ObjectAPI preserves real object-layout metadata through `field_details`
  (`id/type/level/data/terminator/value`) so ability/upgrade/doodad level fields
  can roundtrip without losing structure
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
- global default writes validate and normalize scalar input before staging:
  integer, real, boolean, and string
- smoke harness can perform reversible scalar global validation by name in the
  current live session
- read / write object-editor fields
- object-editor field metadata can be queried safely through Agent RPC
- save / compile from CLI through the live editor session

### GUI-First Proof

The refactor already proved that the Agent path edits real GUI map content:

- GUI-only item-combine formulas were created
- GUI-only item grant was created
- GUI-only compose counter increment was created
- no custom helper script is required for the final GUI-only demo behavior

Reference demo artifact:

- `Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x`

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

Get one global by name:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py global_info udg_compose_count
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

### 8. Reversible Global Smoke

Run a live-session reversible scalar global check by name:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_smoke.py --restore-global --global-name udg_compose_count --global-value 17
```

This mutates one scalar global, verifies readback, then restores the original
value in the same session. Normal `.w3x` sessions auto-save between write and
verify; LNI marker sessions use the live file-backed path.

### 9. Live Regression Harness

Use this as the default operator-facing regression entry once a target map is
open in the refactor debug editor:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_live_regression.py --no-launch --map Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x
```

The harness verifies `editor.current_map_path`, runs `editor.save_map`, writes
and restores `udg_compose_count`, and returns nonzero on failure.

It can also launch `YDWE.exe` itself when no Agent is already running, but this
is currently only a launch-path check. For scalar/pending regression, prefer
`--no-launch` after the target map is visible in the editor:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_live_regression.py --map Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x --close-launched
```

If an Agent is already listening, the launch mode fails before opening another
editor process. Use `--no-launch` for the current session.

Broader P1/P2 regression:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_live_regression.py --no-launch --map Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x --check-global udg_compose_count=17 --check-global udg_compose_stage="p1_stage" --check-global udg_compose_ratio=2.75 --check-global udg_compose_enabled=false --check-pending-clear --check-trigger-rename --trigger-index 0 --check-object-field-map item --check-object-field-map unit
```

## Verified Results

### Live Session

Verified in real YDWE sessions:

- trigger enumeration works
- trigger rename is reversible
- global names/types/values are readable
- LNI live session global write/readback works without native memory writes
- normal `.w3x` global write/readback works without native memory writes
- direct `YDWE.exe <map>` launch opens the requested normal `.w3x` target in the
  debug/refactor startup path
- `ydagent_smoke.py --restore-global --global-name udg_compose_count --global-value 17`
  completed against `compose_demo_gui_only_v2.w3x` and restored the original
  value
- `ydagent_live_regression.py --no-launch --map Q:\AppData\ydwe\work\compose_demo_gui_only_v2.w3x`
  completed against the current live session and restored the original value
- `ydagent_live_regression.py --no-launch --map Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x --check-global udg_compose_count=17 --check-global udg_compose_stage="p0_stage" --check-global udg_compose_ratio=2.75 --check-global udg_compose_enabled=false --check-pending-clear`
  completed against the current live session; integer/string/real/boolean
  scalar writeback restored cleanly, and pending override single-clear/all-clear
  both passed
- `ydagent_live_regression.py --no-launch --map Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x --check-global udg_compose_count=17 --check-global udg_compose_stage="p1_stage" --check-global udg_compose_ratio=2.75 --check-global udg_compose_enabled=false --check-pending-clear --check-trigger-rename --trigger-index 0 --check-object-field-map item --check-object-field-map unit`
  completed against the current live session; trigger rename restored to
  `begin`, object field metadata returned for item and unit, globals restored,
  and pending sidecar remained empty
- default launch mode refuses to start another editor while an Agent is already
  listening on the target port
- save/compile can be triggered from CLI
- object-editor and trigger-editor changes can be materialized into real maps
- real-map object archive reads are live-validated for `item` and `unit`
- `object.read item Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x`
  returned the real custom item `I003` from `war3map.w3t`, including
  `field_details`
- `object.read unit Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x`
  returned real unit records from `war3map.w3u`, including `field_details`

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

- target session: `Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x`
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
- cold self-launch can bring the Agent online before native trigger/global
  capture is ready; use `ydagent_live_regression.py --no-launch` as the reliable
  P0 regression path after the target map is visibly loaded
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
- object-editor archive writeback has a map-lock boundary
  - `object.read` is enabled for extracted real `.w3x` archive object files
  - ObjectAPI read/write is unit-tested for legacy fixture data, real `.w3t`,
    real `.w3a`, malformed tail data, and truncated legacy files
  - `object.write` can write an extracted/current object file and can replace a
    map archive object file when StormLib can open the archive in write mode
  - when the same `.w3x` is open in WorldEdit, StormLib may refuse write-mode
    archive open; in that case `object.write` returns a clear error before
    mutating the cache
  - the remaining hard part is true in-editor object-editor mutation while the
    map archive is locked by the editor

## Next-Phase Goals

### P0

- current active target: harden safe global writeback into an operator-friendly
  feature, not just a proof
- operator-facing live regression entry now exists; next work should extend it
  beyond the compose-count scalar check only after the current path stays stable
- pending override management is exposed through CLI and validated against a
  real pending sidecar for both single-global clear and map-wide clear
- scalar input validation now covers integer, real, boolean, and string
- live validation covers integer, string, real, and boolean scalar globals in a
  normal `.w3x` session
- keep direct native memory writes disabled unless a proven safe native path
  exists

### P1

- trigger rename is now part of the live regression harness and verified
  reversible on the GUI-only demo map
- fresh-session verification is still limited by cold native trigger/global
  capture; the reliable automated path remains `--no-launch` after map load
- keep GUI-trigger-first test cases as the primary proof path

### P2

- object-editor field metadata lookup and real archive object reads are now part
  of live validation
- next object-editor work is a lock-safe in-editor write path for currently open
  `.w3x` maps, either by locating the editor-owned object temp source or by
  adding a save-pipeline hook that stages object replacements before packing
- keep `docs/refactor-status.md` as the only status document

## Documentation Rule

When the refactor state changes:

- update this file
- update `README.md` only if the user-facing capability summary changed
- do not add new report-style markdown files
