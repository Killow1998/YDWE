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
- stable `YDTrigger` debug build

### Agent and Editor Integration

- JSON-RPC worker inside the live YDWE session
- CLI-triggered `save_map`
- live trigger enumeration
- live trigger rename
- live global enumeration
- map-level global create / modify / delete
- object-editor read / write
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
- the safe write path is still file-backed edit -> save/compile -> RPC readback

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

## Minimal Verification

After opening a map in `YDWE.exe`:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py save_map
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py rpc agent.list_triggers
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py rpc agent.list_globals
```

## Status Source

The only maintained refactor status document is:

- [docs/refactor-status.md](./docs/refactor-status.md)
