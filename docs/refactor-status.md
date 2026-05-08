# YDWE Refactor Status

Last updated: 2026-05-09

## Current Repository State

- active branch: `master`
- tracked push target: `killow/master`
- upstream source remote also exists as `origin`

The repo has already landed and pushed the current Agent/runtime/doc/tooling
changes. Generated scratch output should be kept out of future commits.

## Delivered Changes

### Build and Toolchain

- VS2022 / `v143` build path is wired into the Lua build driver
- core debug builds for `YDTrigger` and test targets are working

### Agent Runtime

- `editor.save_map` is implemented in `YDAgentServerWorker.lua`
- `ydagent_client.py save_map` is available
- script global parsing now reads both `globals` and `InitGlobals`
- provider config UI exists in the editor
- AI apply flow has snapshot/rollback support

### Verified Map Editing

- real trigger listing works
- real trigger rename works and is reversible
- real global listing works
- map-level global create / modify / delete works
- object-editor mutation works
- GUI-trigger-only item-combine demo was successfully produced and tested

## Verified Runtime Results

### Example Map Session

For `Development\Component\example(演示地图)\AI\AI——RPG佣兵AI.w3x`:

- before save: trigger/global counts can be `0`
- after `save_map`: counts become non-zero
- real trigger names were read successfully
- real global names/types/values were read successfully

This example map does not need to be kept as a repo change. It is only a
verification target.

### GUI-Only Compose Demo

Primary demo artifact:

- `Q:\AppData\ydwe\work\compose_demo_gui_only_v2.w3x`

Verified behavior:

- formulas are defined in GUI triggers
- item grant is defined in GUI triggers
- compose counter uses a GUI variable increment
- no custom helper script is required for the final GUI-only demo behavior

Related tool:

- `Development/AI/ydmap_compose_demo.py`

## Remaining Gaps

### Not Yet Solved

- runtime direct global-value memory write is still disabled
- a safe native write path for `ydt_set_global_value` still needs design

### Operational Risks

- save/compile automation is stable now, but editor automation remains
  environment-sensitive
- generated logs and scratch files can easily pollute the worktree if not
  cleaned after testing

## Recommended Next Work

1. keep new documentation only under `docs/`
2. keep demo/test artifacts outside the source tree unless they are intentional
3. design a safe native global-write path before re-enabling runtime writes
4. continue testing with GUI-trigger-first scenarios, because that is the
   clearest proof that the Agent path edits real map content
