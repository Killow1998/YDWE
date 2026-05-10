# YDWE

[![Build status](https://ci.appveyor.com/api/projects/status/ybeps6jwp0nupxu6?svg=true)](https://ci.appveyor.com/project/actboy168/YDWE)

YDWE is a Warcraft III World Editor extension toolchain. The current
`ydwe-refactor` branch has already moved the project onto a usable modern
build/runtime baseline and has shipped a working in-editor Agent execution path.

The maintained project documentation is consolidated under
[`docs/`](./docs/README.md).

## What `ydwe-refactor` Delivers Now

### Build and Runtime

- Visual Studio 2022 / `v143` / C++20 build path
- working `YDWE.exe` launch path for runtime verification
- direct `YDWE.exe <map.w3x>` launch now normalizes to `-loadfile <map>` in the
  refactor startup path
- stable `YDTrigger` debug build

### Agent and Editor Integration

- JSON-RPC worker inside the live YDWE session
- CLI-triggered `save_map`
- live trigger enumeration
- live trigger rename
- live global enumeration
- staged global override inspection and clearing for normal `.w3x` sessions
- scalar global-value validation for integer / real / boolean / string defaults
- name-based global lookup/write through Agent RPC and CLI
- live regression harness for map launch/session verification, save/compile,
  reversible scalar global writeback, pending global override cleanup,
  reversible trigger rename, and object-editor field metadata checks
- LNI-session live global write + immediate readback
- normal `.w3x` scalar global write + save/compile + reopen readback
- LNI temp-script save failures caused by leaked control bytes are sanitized at
  compile entry
- Agent `save_map` no-ops on LNI marker maps to avoid rewriting source-backed
  LNI directories
- map-level global create / modify / delete
- object-editor archive read for real `.w3x` object files
- object-editor write in open `.w3x` sessions through pending object staging and
  the normal `save_map` packing pipeline
- object-editor field metadata lookup is covered in live regression
- provider configuration UI
- AI apply snapshot / rollback flow

### Real GUI Map Editing Proof

The current refactor has already proven real GUI-content editing, not just mock
or stub calls:

- GUI trigger creation works
- GUI-only item-combine logic was built and tested
- GUI-only item grant was built and tested
- GUI global counter logic was built and tested

### Current Limitation

- native runtime direct global-value write is still intentionally disabled
- current safe global-write behavior is split by session type:
  - LNI marker map: file-backed live write + immediate RPC readback
  - normal `.w3x`: WTG-backed scalar default write during save/compile, verified
    by reopen readback
- array-global writes are not supported yet

## Quick Links

- [Documentation Index](./docs/README.md)
- [Project Overview](./docs/overview.md)
- [Agent Runtime](./docs/agent-runtime.md)
- [Refactor Status](./docs/refactor-status.md)
- [Development Guide](./docs/development-guide.md)

## Build

```powershell
MSBuild YDWE.sln /t:Build /p:Configuration=Debug /p:Platform=Win32
```

## Run

Use:

```powershell
Q:\AppData\ydwe\YDWE\Build\publish\Debug\YDWE.exe
```

Do not directly start `worldedit.exe` for Agent/runtime validation.

To open a target map directly through the debug startup path:

```powershell
Q:\AppData\ydwe\YDWE\Build\publish\Debug\YDWE.exe Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x
```

## Minimal Verification

After opening a map in `YDWE.exe`:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py save_map
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py rpc agent.list_triggers
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py rpc agent.list_globals
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py global_info udg_compose_count
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py pending_globals
```

For the standard compose demo map, run the live regression harness against an
already-open session:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_live_regression.py --no-launch --map Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x
```

For the P0 scalar/pending regression set:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_live_regression.py --no-launch --map Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x --check-global udg_compose_count=17 --check-global udg_compose_stage="p0_stage" --check-global udg_compose_ratio=2.75 --check-global udg_compose_enabled=false --check-pending-clear
```

For the broader P1/P2 live regression:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_live_regression.py --no-launch --map Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x --internal-usable
```

For the current internal-usable baseline, use a copied working map. This can
self-launch YDWE and close the launched session:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_live_regression.py --map Q:\AppData\ydwe\work\internal_usable_self_launch.w3x --internal-usable --close-launched --wait 60
```

The reliable regression path is `--no-launch` after the target map is visible in
the refactor debug editor. Cold self-launch can bring the Agent online before
native trigger/global capture is ready, so use it only for launch-path checks.

Object archive reads are enabled for real `.w3x` maps. Object writes work while
the map is open by staging the modified `war3map.w3*` file and applying it in
the next `save_map` pack cycle. The internal regression profile covers
item/unit/ability string object fields plus an ability numeric object field.
For manual CLI use, `object_write <type> <map> <json> --save` writes and then
triggers the save pipeline.

## Status Source

The only maintained refactor status document is:

- [docs/refactor-status.md](./docs/refactor-status.md)
