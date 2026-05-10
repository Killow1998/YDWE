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
- run the native-build preflight before treating C++/DLL changes as verified:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_build_preflight.py --build-ydtrigger --build-tests
```

On the current machine this preflight finds only MSBuild 12.0 and fails with
`v143 toolset not found`; native source changes are therefore committed but not
rebuilt into the local `Build/publish/Debug` runtime yet.
- `.github/workflows/ydagent-native.yml` runs the same preflight, focused native
  build, Python syntax checks, and Agent stub loopback on GitHub
  `windows-2022`, which has the correct VS2022/v143 toolchain available.

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

## Internal Usable Milestone

Goal: make `ydwe-refactor` stable enough for internal map development on copied
working maps without frequent manual rescue.

### Acceptance Criteria

- one documented debug build/run path works from a clean checkout on the current
  machine
- one documented live regression command verifies the current standard test map
  without manual clicks after the map is visible in `YDWE.exe`
- the regression command covers:
  - `editor.save_map`
  - scalar global write/restore for integer, real, boolean, and string
  - pending global clear behavior
  - trigger rename/restore
  - object archive read
  - object write/save/readback/restore for at least item, unit, and ability
  - ability numeric object-field write/save/readback/restore
- all generated pending sidecars are inspectable from CLI and are cleared after
  successful save/restore
- failures must be explicit:
  - no silent cache-only object writes
  - no silent pending sidecar corruption
  - no direct `worldedit.exe` launch in validation docs
- docs stay centralized:
  - `docs/refactor-status.md` is the only status source
  - `README.md` only contains the user-facing summary and common commands
- internal users can safely start from a copied `.w3x`, run Agent edits, run the
  regression command, and know whether the map is still valid

### Non-Goals For This Milestone

- polished public UI
- full Release installer
- array global writes
- direct native runtime global-memory writes
- complete GUI trigger AST editing for every ECA shape
- exhaustive object-editor coverage for every field in every object type

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
- `object.write` now supports open-editor `.w3x` sessions by staging modified
  object files into `logs/ydagent_pending_objects.lua`; the save pipeline copies
  staged `war3map.w3*` files into the active temp map before packing
- global parsing reads both `globals` and `InitGlobals`
- when the native WorldEditor global table is empty, Agent global lookup falls
  back to `logs/currentmapscript.j` so normal `.w3x` sessions can still resolve
  scalar globals after save/compile
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
- Agent object type mapping is loopback-checked for all exposed object-editor
  types: `unit`, `item`, `destructable`/`destructible`, `doodad`, `ability`,
  `buff`, and `upgrade`
- `ydagent_live_regression.py --check-object-types` verifies the live Agent
  canonical object-type mapping for every exposed object-editor archive file,
  and `--internal-usable` enables this check by default
- Agent trigger-structure editing is loopback-checked for event, condition, and
  action ECA lists: add ECA, change function name, change parameter value,
  change active state, remove ECA, and verify the count returns to the original
  value
- Agent failure recovery is loopback-checked for ECA structure edits: if a plan
  adds an action ECA and a later operation fails, rollback removes the added ECA
  and restores the original ECA count
- Agent failure recovery is also loopback-checked for ECA active-state edits:
  the native source now exposes `ydt_get_eca_active`, rollback restores the
  captured active value, and the stub verifies active returns to its original
  state after a later operation fails
- `remove_eca` is explicitly treated as non-rollback-safe in `ai.apply_plan`.
  Default apply rejects it before mutation unless `allow_non_recoverable=true`
  is passed, because the current native layer cannot safely restore the removed
  GUI node after a later operation fails.
- `ydagent_live_regression.py --check-trigger-structure` exists for real GUI
  sessions. It should be enabled after rebuilding `YDTrigger.dll` with the
  event/condition-safe `ydt_add_eca` fix. The live check covers add ECA,
  function-name edit, active-state edit/restore, parameter edit when parameters
  exist, remove ECA, and count restore.

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
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_build_preflight.py --build-ydtrigger --build-tests
MSBuild YDWE.sln /t:Build /p:Configuration=Debug /p:Platform=Win32
```

If the preflight fails, install Visual Studio 2022 Build Tools with the C++
workload before rerunning native build/live trigger-structure verification.

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

Pending object replacements created by `object.write` while the map archive is
locked can be inspected with:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py pending_objects Q:\path\to\map.w3x
```

For one-shot CLI usage, `object_write` can trigger the save pipeline
immediately:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py object_write item Q:\path\to\map.w3x item.json --save
```

Clear staged object replacements after an interrupted test:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py clear_pending_objects Q:\path\to\map.w3x war3map.w3t
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py clear_pending_objects --all
```

### 8. Reversible Global Smoke

Run a live-session reversible scalar global check by name:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_smoke.py --restore-global --global-name udg_compose_count --global-value 17
```

This mutates one scalar global, verifies readback, then restores the original
value in the same session. Normal `.w3x` sessions auto-save between write and
verify; LNI marker sessions use the live file-backed path.

### 9. Internal Usable Regression

The current internal-usable baseline can self-launch YDWE, validate a copied
test map, and close the launched session. Use `--copy-from` so the source map is
not mutated:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_live_regression.py --copy-from Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x --map Q:\AppData\ydwe\work\internal_usable_copy_from.w3x --internal-usable --close-launched --wait 60
```

This command mutates and restores data on the copied target map only.

For an already-open editor session, use:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_live_regression.py --no-launch --map Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x --internal-usable
```

If an Agent is already listening, launch mode fails before opening another
editor process. Use `--no-launch` for the current session.

Minimal smoke remains available:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_live_regression.py --no-launch --map Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x
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
- `ydagent_live_regression.py --no-launch --map Q:\AppData\ydwe\work\object_write_regression.w3x --check-object-write item`
  completed against an open editor session; it mutated item `unam`, staged the
  object file while the archive was locked, saved through the editor, verified
  readback, restored the original object JSON, and saved again
- `ydagent_live_regression.py --no-launch --map Q:\AppData\ydwe\work\internal_usable_regression.w3x --check-object-write item --check-object-write unit`
  completed against an open editor session; item `unam` and unit `uabi` were
  mutated, saved, verified, restored, and saved again
- `ydagent_live_regression.py --no-launch --map Q:\AppData\ydwe\work\internal_usable_regression.w3x --check-global udg_compose_count=17 --check-global udg_compose_stage="internal_stage" --check-global udg_compose_ratio=2.75 --check-global udg_compose_enabled=false --check-pending-clear --check-trigger-rename --trigger-index 0 --check-object-field-map item --check-object-field-map unit --check-object-read item --check-object-read unit --check-object-write item --check-object-write unit`
  completed as the internal-usable baseline; it covered save, four scalar
  global types, pending global clear, reversible trigger rename, item/unit
  object archive reads, item/unit staged object writes, and item/unit field maps
- `ydagent_live_regression.py --no-launch --map Q:\AppData\ydwe\work\internal_usable_regression_ext.w3x --check-global udg_compose_count=17 --check-object-read item --check-object-read unit --check-object-read ability --check-object-write item --check-object-write unit --check-object-write ability`
  completed as extended object-editor coverage; item `unam`, unit `uabi`, and
  ability `anam` were staged, saved, verified, restored, and saved again
- `ydagent_live_regression.py --no-launch --map Q:\AppData\ydwe\work\internal_usable_profile.w3x --internal-usable`
  completed as the short-form internal-usable profile; it expands to scalar
  global checks, pending clear, trigger rename, item/unit/ability object
  read/write, and item/unit field-map checks
- `ydagent_live_regression.py --no-launch --map Q:\AppData\ydwe\work\internal_usable_numeric.w3x --internal-usable`
  completed after adding numeric object-field coverage; it additionally mutated,
  saved, verified, and restored ability numeric field `achd`
- `ydagent_live_regression.py --map Q:\AppData\ydwe\work\internal_usable_self_launch.w3x --internal-usable --close-launched --wait 60`
  completed without manual editor setup; it launched YDWE, ran the full
  internal-usable profile, closed the launched session, and left no pending
  sidecars behind
- `ydagent_live_regression.py --copy-from Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x --map Q:\AppData\ydwe\work\internal_usable_copy_from.w3x --internal-usable --close-launched --wait 60`
  completed as the one-command internal test; it copied the source map, launched
  YDWE, ran the full internal-usable profile on the copy, closed the launched
  session, and left no pending sidecars behind
- `ydagent_tui.py stub --port 27119` completed after adding script-global
  fallback support; it kept the stub Agent RPC baseline at 13/13 passing
- `ydagent_tui.py stub --port 27119` completed after adding full object type
  mapping checks, `object_set_field` validation for destructable objects, and
  apply-plan failure rollback coverage; it kept the stub Agent RPC baseline at
  17/17 passing
- `ydagent_tui.py stub --port 27119` completed after adding event/condition/action
  ECA structure-edit checks; it kept the stub Agent RPC baseline at 44/44
  passing
- `ydagent_tui.py stub --port 27119` completed after adding ECA add rollback
  coverage; it kept the stub Agent RPC baseline at 47/47 passing
- `ydagent_tui.py stub --port 27119` completed after adding ECA active getter
  and active-state rollback coverage; it kept the stub Agent RPC baseline at
  59/59 passing
- `ydagent_tui.py stub --port 27119` completed after adding the
  non-rollback-safe `remove_eca` apply rejection check; the stub verifies the
  action ECA count is unchanged after rejection
- `ydagent_live_regression.py --internal-usable` now includes live
  `object.types` canonical mapping verification for all exposed object-editor
  types before map-specific object read/write checks
- `ydagent_live_regression.py --copy-from ... --internal-usable --close-launched`
  reached trigger structure validation in a real GUI session, exposed that the
  current built `YDTrigger.dll` adds an action node even when asked to add an
  event node, and the source fix now restricts the `CommentString` safe clone to
  action inserts only. Rebuild with v143 before treating live trigger-structure
  coverage as complete.
- `ydagent_build_preflight.py` now makes the native build prerequisite explicit.
  On this machine it reports only `C:\Program Files (x86)\MSBuild\12.0\Bin\MSBuild.exe`
  and fails because the v143 toolset is not installed.
- `ydagent-native.yml` is available as a remote native gate for environments
  where VS2022/v143 is present. It verifies `YDTrigger.vcxproj`,
  `YDWE_Test.vcxproj`, executes `YDWE_Test.exe`, and runs the `ydagent_tui.py`
  stub loopback.
- `test_object_api.cpp` now contains real-layout roundtrip fixtures for every
  object-editor file type (`w3u`, `w3t`, `w3b`, `w3d`, `w3a`, `w3h`, `w3q`);
  rerun `YDWE_Test` in an environment with MSBuild available

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
- cold self-launch is now validated by the internal-usable profile, but it still
  requires no existing Agent on the target port and should use a copied map
- a failed cold self-launch can leave an editor process holding the default
  Agent port and test map lock; close that editor before rerunning the
  one-command GUI regression, or the next run can connect to stale worker code
- `--copy-from` now reports locked target-map copy failures as explicit
  regression errors instead of leaking a Python traceback
- the current machine only has MSBuild 12.0/v120 installed; `YDWE_Test.vcxproj`
  requires v143. Forcing `PlatformToolset=v120` also fails because the bundled
  Catch2 headers use newer C++ syntax, so native C++ test changes must be built
  on a machine with the v143 toolset
- forcing `YDTrigger.vcxproj` to `PlatformToolset=v120` also fails before the
  DLL build reaches YDTrigger sources, because `bee.lua` dependency sources use
  C inline syntax unsupported by that toolchain; the native ECA fix needs a v143
  rebuild before live GUI verification can pass
- run `ydagent_build_preflight.py --build-ydtrigger --build-tests` before any
  claim that native trigger-structure changes are available in the local runtime
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
- object-editor archive writeback is staged when the map is open
  - `object.read` is enabled for extracted real `.w3x` archive object files
  - object type mapping follows the Warcraft object file order:
    `unit`, `item`, `destructable`/`destructible`, `doodad`, `ability`,
    `buff`, `upgrade`
  - `ObjectAPI.h` uses the same canonical mapping; `w3b` is
    destructable/destructible and `w3h` is buff
  - ObjectAPI read/write is unit-tested for legacy fixture data, real `.w3t`,
    real `.w3a`, malformed tail data, and truncated legacy files
  - `object.write` can write an extracted/current object file and can replace a
    map archive object file when StormLib can open the archive in write mode
  - when the same `.w3x` is open in WorldEdit, `object.write` writes a cache
    file and registers it in `logs/ydagent_pending_objects.lua`; `save_map`
    applies it into `w3xTemp` before `map_packer`
  - if a staged object cache file is deleted before save, the save pipeline
    fails rather than silently producing a partial map

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
- trigger ECA structure editing has stub coverage for event/condition/action
  add, function edit, active-state edit, parameter edit, remove,
  add-then-fail rollback, and active-state rollback, but real GUI coverage is
  blocked until `YDTrigger.dll` is rebuilt with the v143 toolset
- `remove_eca` rollback is intentionally not implemented yet; default
  `ai.apply_plan` refuses it as non-rollback-safe unless the caller explicitly
  opts into `allow_non_recoverable=true`
- fresh-session verification is still limited by cold native trigger/global
  capture; the reliable automated path remains `--no-launch` after map load
- keep GUI-trigger-first test cases as the primary proof path

### P2

- object-editor field metadata lookup, real archive object reads, and staged
  open-editor object writes are now part of live validation
- next object-editor work is broadening write regression beyond the current
  ability numeric field to explicit level/data-sensitive fields and upgrade
  fields
- keep `docs/refactor-status.md` as the only status document

## Documentation Rule

When the refactor state changes:

- update this file
- update `README.md` only if the user-facing capability summary changed
- do not add new report-style markdown files
