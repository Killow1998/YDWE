# YDWE

[![Build status](https://ci.appveyor.com/api/projects/status/ybeps6jwp0nupxu6?svg=true)](https://ci.appveyor.com/project/actboy168/YDWE)

YDWE is a Warcraft III World Editor extension toolchain. The maintained project
documentation is now consolidated under [`docs/`](./docs/README.md).

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

Build requirements and runtime notes are maintained in
[`docs/overview.md`](./docs/overview.md).
