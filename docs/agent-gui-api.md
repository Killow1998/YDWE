# Agent GUI API Manual

This document defines how agents must edit real WorldEditor GUI content in
`ydwe-refactor`. It is an API manual, not a progress report.

## Contract

- Use `ai.operation_schema` before generating a plan.
- Apply changes through `ai.apply_plan`; do not directly patch map files for GUI
  trigger work unless a documented helper explicitly says so.
- Use semantic templates for gameplay systems. Do not invent raw GUI function
  names or parameter layouts from memory.
- Verify every non-trivial map edit with `editor.save_map` and a loopback read.
- Treat `remove_eca` as non-rollback-safe. It requires
  `allow_non_recoverable=true` and an explicit reason.
- Start validation from `YDWE.exe`, not `worldedit.exe`.

## Stable Low-Level Operations

These are exposed by `ai.operation_schema` and are currently executable through
`ai.apply_plan`.

| Operation | Purpose | Rollback |
| --- | --- | --- |
| `set_trigger_name` | Rename an existing GUI trigger. | Yes |
| `set_trigger_disabled` | Enable or disable a GUI trigger. | Yes |
| `add_eca` | Add an event, condition, or action node. | Yes |
| `set_eca_func_name` | Change a GUI ECA function name. | Yes |
| `set_eca_active` | Enable or disable a GUI ECA node. | Yes |
| `set_eca_param_value` | Change one ECA parameter value. | Yes |
| `remove_eca` | Remove an ECA node. | No |
| `object_set_field` | Change an object-editor field. | Best effort |

`eca_type` values:

| Value | Meaning |
| --- | --- |
| `0` | event |
| `1` | condition |
| `2` | action |

## Semantic Template Status

The runtime schema exposes these template names so future agents can discover
the intended API surface. Templates marked `planned` are not a promise that a
single high-level RPC already exists; until promoted to `stable`, an agent must
compile the template into the stable low-level operations above and verify the
generated GUI ECA list.

| Template | Status | Use Case |
| --- | --- | --- |
| `quest.create` | planned | Create a quest, set title/description/icon, store handle. |
| `quest.complete_when` | planned | Mark a quest or quest item complete from a GUI condition. |
| `creep_spawn.periodic` | planned | Periodic region-based creep spawn with a count cap. |
| `leaderboard.create_or_update` | planned | Create and update a leaderboard from globals. |
| `timer_window.countdown` | planned | Create a timer, timer dialog, and expire trigger. |
| `dialog.choice` | planned | Create a dialog, add buttons, and handle selections. |

## Template Requirements

### `quest.create`

Inputs:

- `trigger`: target trigger or new trigger name
- `quest_global`: `quest` global to store the quest handle
- `title`
- `description`
- `icon_path`
- `required`: boolean

Expected GUI actions:

- create quest
- assign `Last created quest` to `quest_global`
- set title
- set description
- set icon path
- optionally mark required/discovered

Verification:

- `agent.list_globals` includes `quest_global`
- trigger action list contains quest create and property actions
- `editor.save_map` succeeds

### `quest.complete_when`

Inputs:

- `condition`: GUI condition template
- `quest_global`
- optional `quest_item_global`
- optional completion message

Expected GUI structure:

- event that drives the completion check
- condition ECA matching the requested completion condition
- action to mark quest or quest item complete
- optional message action

Verification:

- condition and action counts match the planned structure
- completion action references the correct global
- `editor.save_map` succeeds

### `creep_spawn.periodic`

Inputs:

- `region_global`
- `unit_id`
- `owner_player`
- `interval_seconds`
- `spawn_count`
- `max_alive`
- optional `group_global`

Expected GUI structure:

- periodic timer event
- condition checking alive count below cap
- action creating units at region center or random point
- optional group registration/cleanup

Verification:

- timer event exists
- cap condition exists
- create-unit action uses the requested unit id and count
- `editor.save_map` succeeds

### `leaderboard.create_or_update`

Inputs:

- `leaderboard_global`
- `title`
- rows: player/global pairs
- display flag

Expected GUI structure:

- create leaderboard
- store handle in `leaderboard_global`
- add rows
- update row values
- display leaderboard

Verification:

- leaderboard global exists
- create/update/display actions exist
- `editor.save_map` succeeds

### `timer_window.countdown`

Inputs:

- `timer_global`
- `timer_dialog_global`
- `duration_seconds`
- `title`
- timeout trigger/action list

Expected GUI structure:

- create timer dialog
- store timer and timer dialog handles
- start timer
- timeout event
- cleanup/destroy dialog when finished

Verification:

- timer and timer dialog globals exist
- expire event references the timer
- cleanup action exists
- `editor.save_map` succeeds

### `dialog.choice`

Inputs:

- `dialog_global`
- `message`
- buttons: text/global pairs
- per-button response action lists

Expected GUI structure:

- set dialog message
- add buttons
- store button handles
- display dialog
- create per-button clicked events or equivalent branching

Verification:

- dialog and button globals exist
- each button has a response path
- `editor.save_map` succeeds

## Required Validation Flow

For a real map edit:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py rpc ai.operation_schema
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py rpc ai.validate_plan '<plan-json>'
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py save_map
```

For regression after Agent code changes:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_build_preflight.py --check-runtime-exports
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_tui.py stub --port 27119
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_live_regression.py --copy-from Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x --map Q:\AppData\ydwe\work\agent_gui_api_check.w3x --internal-usable --check-trigger-structure --close-launched --wait 60
```

## Current Gap

The low-level GUI ECA editor is working. The semantic template names are now
published in `ai.operation_schema`, but the high-level one-call RPCs for these
templates are still planned. Until those RPCs are implemented, agents must use
this document to compile semantic templates into low-level `ai.apply_plan`
operations and verify the resulting GUI structure.
