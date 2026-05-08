# YDWE Overview

## What It Is

YDWE is a secondary development layer on top of Warcraft III World Editor. The
current repo contains the editor launcher, Lua plugin runtime, GUI trigger
extensions, LSP integration, and the in-editor Agent execution path.

## Build Requirements

- Visual Studio 2022
- Win32 toolchain with `v143`
- C++20 support
- Windows 10 SDK 10.0+

Primary build entry:

```powershell
MSBuild YDWE.sln /t:Build /p:Configuration=Debug /p:Platform=Win32
```

The Lua build path also assumes `Build/lua/make.lua` drives MSBuild with
`PlatformToolset = v143`.

## Runtime Launch

Use:

```powershell
Q:\AppData\ydwe\YDWE\Build\publish\Debug\YDWE.exe
```

Do not directly launch `worldedit.exe` for Agent/runtime verification. YDWE
must spawn the editor process itself.

## Important Outputs

- `Build\publish\Debug\YDWE.exe`
- `Build\publish\Debug\bin\worldeditydwe.exe`
- `Build\publish\Debug\plugin\YDTrigger.dll`
- `Build\publish\Debug\bin\ydwe-lsp.exe`
- `Build\publish\Debug\bin\lsp_client.dll`

## Repository Layout

- `Build/`
  - generated publish tree and build scripts
- `Development/Component/`
  - runtime scripts, plugin configs, packaged editor assets
- `Development/Plugin/WE/YDTrigger/`
  - GUI trigger native layer
- `Development/AI/`
  - CLI tools, test harnesses, demo-map generator
- `Development/Test/`
  - C++ tests
- `docs/`
  - maintained project documentation

## Current Operational Rules

- For editor automation, start exactly one `YDWE.exe` session.
- Use `rtk` as the command prefix in local shell workflows.
- Treat generated logs and scratch files as disposable; they should not be kept
  as documentation.

## Related External Projects

This repo still depends on or integrates with:

- `ydhost`
- `vscode-lua-debug`
- `lni`
- `lml`
- `w3xparser`
- `pjass-chs`
- `Direct3D8to9`
- `w3x2lni`
- `yue`
- `wave`

For third-party component details, read the README files under `OpenSource/`.
