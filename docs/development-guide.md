# YDWE Development Guide

## Scope

This file replaces the previous split between code-style notes, performance
notes, and refactor-side working rules.

## Build Rules

- target toolchain: Visual Studio 2022 + `v143`
- target language level: C++20
- target platform: Win32
- keep build entrypoints aligned with `Build/lua/make.lua`

## Runtime Rules

- launch editor automation through `YDWE.exe`
- do not directly drive `worldedit.exe` for Agent verification
- clean test processes and generated scratch files after each run

## Coding Style

- prefer modern C++ features that are already in use in the repo
- keep naming and file-local style aligned with surrounding code
- use concise comments only where logic is not obvious
- keep Lua plugin APIs small and explicit
- keep RPC behavior type-aware and fail-closed when safety is unclear

## Modernization Guidance

Applied direction:

- replace legacy utility patterns with standard-library equivalents where safe
- keep memory-sensitive hook code conservative
- prefer rollback-capable editor operations over blind mutation
- prefer file-backed edits when runtime memory mutation is unsafe

## Performance Guidance

Performance work should remain subordinate to editor stability. In this repo,
that means:

- do not trade save integrity for direct memory mutation speed
- keep parsing and object snapshots scoped to the active operation
- treat large generated artifacts as temporary verification output

## Documentation Rules

- maintain canonical project docs under `docs/`
- keep root `README.md` as a thin entrypoint only
- do not keep parallel status/report/handoff files in the repo root
- fold new verification results into `docs/refactor-status.md`
- fold new runtime architecture changes into `docs/agent-runtime.md`

## Verification Rules

- prefer no-GUI loopback tests first
- then run one real `YDWE.exe` session for integration verification
- keep operations reversible when validating editor mutations
