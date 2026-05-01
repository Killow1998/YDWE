# YDWE LSP Server

YDWE Language Server Protocol implementation for Jass/Lua editing support.

## Quick Start

### Launch YDWE Editor
```
Q:\AppData\ydwe\YDWE\Development\Component\bin\worldedit.exe
```

The editor will automatically:
1. Load Lua engine (`LuaEngine.dll`)
2. Scan plugins in `Component\plugin\*.plcfg`
3. Load `YDLspClient` plugin → start `ydwe-lsp.exe`
4. Load `YDTrigger` plugin → enable extended GUI triggers

### Build All
```
MSBuild YDWE.sln /t:Build /p:Configuration=Debug /p:Platform=Win32
```

Key outputs:
- `Component\bin\ydwe-lsp.exe` — LSP server
- `Component\bin\lsp_client.dll` — Lua C module for LSP client
- `Component\plugin\YDTrigger.dll` — GUI trigger extensions

## Architecture

```
worldedit.exe (YDWE Editor)
├── LuaEngine.dll           Script engine
├── script/ydwe/main.lua    Main script entry
│   └── ydwe_on_startup.lua Plugin loader
│       ├── YDTrigger.plcfg → YDTrigger.dll (GUI trigger hooks)
│       └── YDLspClient.plcfg → lsp_client.dll → ydwe-lsp.exe
└── wehelper.dll            WE helper

ydwe-lsp.exe (LSP Server, stdin/stdout JSON-RPC)
├── server.cpp              Message loop, request handlers
├── completion.cpp          Static completion (keywords/types/BJ)
├── jass_lexer.cpp          Jass tokenizer
└── jass_parser.cpp         Jass parser → JassDocument

lsp_client.dll (Lua C Module)
├── lsp_client.cpp          Process/pipe management
└── luaopen_lsp_client.cpp  Lua bindings
```

## Supported LSP Features

| Feature | Method | Description |
|---------|--------|-------------|
| Initialize | `initialize` | Returns server capabilities |
| Shutdown | `shutdown` | Graceful shutdown |
| Document Sync | `textDocument/didOpen/didChange/didClose/didSave` | Full text sync |
| Completion | `textDocument/completion` | Keywords, types, BJ functions, user-defined symbols |
| Hover | `textDocument/hover` | Function signatures, variable types |
| Go to Definition | `textDocument/definition` | Functions, globals, locals, parameters |
| Document Symbols | `textDocument/documentSymbol` | Functions and globals outline |
| Diagnostics | `textDocument/publishDiagnostics` | Parse errors, duplicate definitions |

## Completion Sources

### Static (built-in)
- Jass keywords: `function`, `if`, `loop`, `set`, `call`, etc.
- Jass types: `integer`, `real`, `string`, `unit`, `handle`, etc.
- BJ common functions: `CreateUnit`, `KillUnit`, `GetUnitX`, `TriggerAddAction`, etc.
- Lua keywords and standard functions

### Dynamic (from parsed document)
- User-defined functions with full signature
- Global variables (with type and array info)
- Local variables (scoped to current function)
- Function parameters (scoped to current function)
- User-defined types (with extends info)

## Lua API (in YDWE scripts)

```lua
local lsp = require 'lsp'

-- Start LSP server
lsp.start("path/to/ydwe-lsp.exe", workspace_path)

-- Document operations
lsp.openDocument(uri, "jass", text)
lsp.changeDocument(uri, startLine, startChar, endLine, endChar, newText)
lsp.closeDocument(uri)

-- Request completion
lsp.requestCompletion(uri, line, character, function(result)
    for _, item in ipairs(result.items) do
        print(item.label, item.detail)
    end
end)

-- Diagnostics callback
lsp.setDiagnosticsCallback(function(params)
    for _, d in ipairs(params.diagnostics) do
        print(d.message, d.range.start.line)
    end
end)
```

## Future Direction: GUI Trigger AI Assistant

The current implementation provides Jass text-level LSP features.
The real value of YDWE Agent is **GUI trigger-level AI assistance**:

- "Create a trigger that gives gold every 5 seconds"
- "Fix memory leaks in this trigger"
- "Convert this GUI trigger to custom script"

This requires understanding the GUI trigger data model (ECA structure),
not just Jass text analysis. The YDTrigger plugin hooks into the WE's
GUI trigger system and provides the foundation for this integration.

### GUI Trigger Data Model (YDTrigger)
- **ECA**: Event/Condition/Action node in trigger editor
- **GUI ID**: Unique identifier for each GUI function (CC_GUIID_*)
- **Parameters**: Typed values attached to ECA nodes
- **Categories**: Event / Condition / Action types

### Agent RPC Additions

The Agent plugin exposes a local JSON-RPC server at `127.0.0.1:27118`.
It is implemented by:

- `Component\plugin\YDAgentCore.lua`
- `Component\plugin\YDAgentServer.lua`
- `Component\plugin\YDAgentServerWorker.lua`
- `Component\plugin\YDAgentFieldMap.lua`
- `Component\plugin\YDAgentAI.lua`
- `Component\plugin\YDAgentOps.lua`

Object editor field metadata is loaded from the localized SLK metadata and
WorldEdit string tables under `Component\share\zh-CN\mpq`.

New object editor methods:

- `object.field_name(field_id, source)`
- `object.field_info(field_id, source)`
- `object.field_map(source)`
- `object.read_annotated(type_name, map_path)`

AI provider methods:

- `ai.configure(provider, options)`
- `ai.status()`
- `ai.build_request(prompt, context, options)`
- `ai.complete(prompt, context, options)`
- `ai.parse_response(provider, response_text)`
- `ai.validate_plan(plan)`
- `ai.operation_schema()`
- `ai.apply_plan(plan, options)`
- `ai.queue_review(plan)`
- `ai.generate_plan(prompt, context, options)`
- `ai.generate_trigger_plan(index, instruction, options)`
- `ai.generate_batch_plan(instruction, context_options, options)`
- `ai.generate_relationship_plan(instruction, context_options, options)`
- `ai.generate_text(prompt, context, options)`
- `ai.generate_code(language, instruction, context, options)`
- `ai.explain_trigger(index, options)`

Trigger AI context methods:

- `agent.summarize_trigger(index)`
- `agent.build_prompt(index, instruction)`
- `agent.compress_context(options)`
- `agent.build_batch_prompt(instruction, options)`
- `agent.analyze_relationships(options)`
- `agent.build_relationship_prompt(instruction, options)`

Supported providers are `claude`, `openai`, `local`, and `local_llm`.
Cloud provider keys are read from `ANTHROPIC_API_KEY` and `OPENAI_API_KEY`
unless explicitly configured by the caller.

AI responses are parsed into a reviewable operation plan. The current safe
operation whitelist includes trigger rename/disable, ECA function/parameter
updates, ECA add/remove, and object editor field updates. The Agent validates
these operations and reports warnings/errors; it does not automatically apply
AI changes in this stage.

Plan application is available through `ai.apply_plan(plan, options)`.
The default mode is dry-run. A non-dry-run apply requires all of:

- `options.dry_run = false`
- `options.confirm = true`
- a one-shot UI approval token granted from the `YDWE AI Agent` menu

The editor menu adds:

- `YDWE AI Agent -> Agent Status`
- `YDWE AI Agent -> Safe Operations`
- `YDWE AI Agent -> Generate Panel`
- `YDWE AI Agent -> Review Panel`
- `YDWE AI Agent -> Apply Workflow`

The review panel displays the latest queued AI operation plan. Plans can be
queued by `ai.generate_plan`, `ai.generate_trigger_plan`,
`ai.generate_batch_plan`, or `ai.queue_review`. The panel can refresh the
queue and grant the one-shot apply token.

For multi-trigger work, `agent.compress_context(options)` limits the number of
triggers and ECA nodes included in AI context. `agent.build_batch_prompt`
packages the compressed context with the safe operation schema.

The generate panel can call the local JSON-RPC server directly. It supports
single-trigger operation plan generation, batch plan generation, relationship
plan generation, trigger explanation, relationship analysis, and JASS text
generation. Relationship analysis is provided by
`agent.analyze_relationships(options)`, which extracts shared identifiers from
ECA function names and parameters.

### Next Steps
1. Build full trigger-to-natural-language converter
2. Build full natural-language-to-trigger generator
3. Add richer result rendering and copy/apply helpers
4. Expand prompt templates and examples

### Follow-up Roadmap Snapshot

Current implementation status:

- Demo-quality safe AI operation loop: about 70% - 80% complete
- Product-quality AI assistant for map authors: about 50% - 60% complete

Remaining work should focus on:

1. Runtime smoke tests and diagnostics
   - Verify menu loading, JSON-RPC server health, UI RPC client, Review Panel,
     Generate Panel, one-shot apply token, and `ai.apply_plan`.
   - Avoid the Debug `lua.exe` syntax-check flow that previously hung.

2. Full trigger-to-natural-language and natural-language-to-trigger conversion
   - Add Chinese explanation templates, localized ECA names, parameter meaning,
     risk detection, and common trigger templates.

3. Review Panel productization
   - Replace raw JSON-only view with operation rows, risk/warning/error display,
     target details, dry-run result, copy helpers, partial approval, and refresh
     after apply.

4. Apply safety and rollback
   - Add pre-apply snapshots, object file backups, stronger ECA index/type
     checks, original-object protection, and better failure reporting.

5. AI provider configuration UI
   - Add provider/model/endpoint fields, API key environment status, and request
     dry-run preview without storing keys in plain text.

6. Object editor AI batch workflow
   - Improve field type validation, metadata-aware prompts, and safer bulk
     modifications.

Recommended implementation order:

1. Smoke tests / diagnostic RPC
2. Structured Review Panel
3. Provider configuration UI
4. Snapshot / backup / stronger validation
5. Trigger explanation templates
6. Natural-language common trigger templates
7. Object editor AI batch workflow
