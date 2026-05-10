# YDWE Agent Runtime

## Runtime Chain

The current editor-side execution chain is:

`YDWE.exe -> worldeditydwe.exe -> LuaEngine -> YDTrigger.dll -> YDAgentServerWorker.lua -> JSON-RPC -> ydagent_client.py`

For code editing support, the LSP side is:

`YDLspClient.plcfg -> lsp_client.dll -> ydwe-lsp.exe`

## Main Files

- `Development/Component/plugin/YDAgentServerWorker.lua`
- `Development/Component/plugin/YDAgentCore.lua`
- `Development/Component/plugin/YDAgentAI.lua`
- `Development/Component/plugin/YDAgentUI.lua`
- `Development/Component/script/ydwe/ydwe_on_menu.lua`
- `Development/AI/ydagent_client.py`

## Supported Agent Capabilities

Verified working:

- list real triggers
- rename real triggers
- read real globals
- create / modify / delete globals at map-file level
- object editor archive read / staged write for real `.w3x` maps
- pending object replacement inspection and cleanup
- trigger `save_map` from CLI through `editor.save_map`
- configure AI providers in the editor UI
- execute AI apply operations with snapshot/rollback support

Verified but intentionally constrained:

- runtime direct global memory write is still disabled in native C++
- safe path is file-level mutation -> save/compile -> read back

## Save and Refresh Flow

Cold-start sessions often have empty trigger/global caches. Use:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py save_map
```

This calls `editor.save_map`, waits for the worker to reconnect, then lets the
RPC layer read the compiled script state.

## Core Commands

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

Rename a trigger:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py set_trigger_name 0 begin__AI_SMOKE__
```

Create / delete globals:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py create_global compose_flag integer 0
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py delete_global compose_flag
```

Read / write object data:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py object_read item Q:\path\to\map.w3x
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py object_write item Q:\path\to\map.w3x item.json --save
```

Inspect or clear staged object replacements:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py pending_objects Q:\path\to\map.w3x
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_client.py clear_pending_objects --all
```

## AI Provider Modes

The current UI/provider layer supports both API-style providers and CLI coding
agents. The CLI presets currently include:

- `gemini_cli`
- `claude_code`
- `copilot_cli`
- `codex`

The configuration entry is exposed from the editor menu as `Provider Config`.

## Loopback Testing

No-GUI stub loopback:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_tui.py stub --restore
```

Real GUI verification:

Self-launching internal baseline:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_live_regression.py --copy-from Q:\path\to\source-map.w3x --map Q:\path\to\copied-map.w3x --internal-usable --close-launched --wait 60
```

Already-open editor session:

```powershell
rtk python Q:\AppData\ydwe\YDWE\Development\AI\ydagent_live_regression.py --no-launch --map Q:\path\to\copied-map.w3x --internal-usable
```

## Current Limits

- global value writes do not use a native memory setter
- array-global writes are not supported
- object-editor regression currently covers item/unit/ability string fields and
  one ability numeric field; upgrade and explicit level/data field cases remain
  future coverage
- direct editor automation must go through `YDWE.exe`, not raw `worldedit.exe`
- generated logs are not a documentation source of truth
