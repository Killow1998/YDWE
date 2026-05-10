# Agent GUI API Manual

This document defines how agents must edit real WorldEditor GUI content in
`ydwe-refactor`. It is an API manual, not a progress report.

## Contract

- Use `ai.operation_schema` before generating a plan.
- Apply GUI gameplay templates through `ai.apply_template`; use
  `ai.template_plan` for preview/dry-run. Use `ai.apply_plan` only for the
  stable low-level operations listed below.
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

The runtime schema exposes these template names and `ai.template_plan` /
`ai.apply_template` compile them into low-level GUI ECA operations. Agents must
call these RPCs for the listed gameplay systems instead of hand-building random
function/parameter layouts.

| Template | Status | Use Case |
| --- | --- | --- |
| `quest.create` | rpc | Create a quest, set title/description/icon, store handle. |
| `quest.complete_when` | rpc | Mark a quest or quest item complete from a GUI condition. |
| `creep_spawn.periodic` | rpc | Periodic region-based creep spawn with a count cap. |
| `leaderboard.create_or_update` | rpc | Create and update a leaderboard from globals. |
| `timer_window.countdown` | rpc | Create a timer, timer dialog, and expire trigger. |
| `dialog.choice` | rpc | Create a dialog, add buttons, and handle selections. |

RPCs:

```text
ai.template_plan(template_name, args)
ai.apply_template(template_name, args, options)
```

`ai.apply_template` accepts the same options as `ai.apply_plan`, including
`dry_run`, `confirm`, and rollback behavior.

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
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py rpc ai.template_plan '<template-name>' '<args-json>'
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py rpc ai.apply_template '<template-name>' '<args-json>' '{"dry_run":true}'
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py save_map
```

For regression after Agent code changes:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_build_preflight.py --check-runtime-exports
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_tui.py stub --port 27119
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_live_regression.py --copy-from Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x --map Q:\AppData\ydwe\work\agent_gui_api_check.w3x --internal-usable --check-trigger-structure --close-launched --wait 60
```

## Native Boundary

The native ECA editor currently creates new GUI nodes by cloning an existing
node of the same ECA type. `ai.apply_template` therefore validates and rolls
back through `ai.apply_plan`; if a target map lacks a suitable GUI parameter
shape for a template, the template apply fails instead of silently writing a
partial trigger. After any successful template apply, run `editor.save_map` and
read the ECA tree back.
