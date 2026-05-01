# YDWE LSP Client Plugin

Lua plugin that integrates the LSP server into YDWE editor.

## Build

```
MSBuild lsp_client.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32
```

Output: `Build\bin\Debug\bin\lsp_client.dll`

## Installation

The plugin is loaded by YDWE via:
1. **Plugin config**: `Component\plugin\YDLspClient.plcfg`
2. **Plugin loader**: `Component\plugin\YDLspClient.lua`
3. **Lua API**: `Component\script\common\lsp.lua`

## Lua API

### Configuration

```lua
local lsp = require "lsp"

lsp.config({
    serverPath = "path/to/ydwe-lsp.exe",
    logLevel = 1,          -- 0=none, 1=error, 2=warn, 3=info
    traceLevel = "off",    -- "off"|"messages"|"verbose"
})
```

### Start/Stop Server

```lua
lsp.start()                -- Start LSP server process
lsp.stop()                 -- Stop LSP server process
lsp.isRunning()            -- Check if server is running
```

### Document Synchronization

```lua
lsp.openDocument(uri, languageId, version, text)
lsp.changeDocument(uri, version, text)
lsp.closeDocument(uri)
```

### LSP Requests

```lua
-- Request completion at position
local items = lsp.requestCompletion(uri, line, character)

-- Request hover info at position
local hover = lsp.requestHover(uri, line, character)

-- Request go-to-definition at position
local locations = lsp.requestDefinition(uri, line, character)
```

### Callbacks

```lua
-- Register callback for diagnostics
lsp.onDiagnostics(function(uri, diagnostics)
    for _, d in ipairs(diagnostics) do
        print(d.message, d.range.start.line)
    end
end)

-- Register callback for log messages
lsp.onLog(function(message)
    print("[LSP]", message)
end)
```

## Architecture

```
YDWE Editor
├── YDLspClient.plcfg          Plugin registration
├── YDLspClient.lua            Plugin loader (loads DLL via FFI)
├── lsp.lua                    Lua integration API
└── lsp_client.dll             C++ client (process/pipe management)
    ├── Process management     CreateProcess, pipes
    ├── Message I/O            JSON-RPC over stdin/stdout
    └── Lua bindings           Expose to Lua via luaopen_lsp_client
```

### Data Flow

```
YDWE Lua Script
    │
    │ (Lua API call)
    ▼
lsp.lua
    │
    │ (FFI call)
    ▼
lsp_client.dll
    │
    │ (JSON-RPC over pipes)
    ▼
ydwe-lsp.exe (LSP Server)
```
