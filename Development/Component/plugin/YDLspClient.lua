-- YDWE LSP Client Plugin Loader
-- Loads lsp_client.dll, starts LSP server, and provides integration

local fs = require "bee.filesystem"
local uni = require "ffi.unicode"

local loader = {}

loader.load = function(path)
    local ok, lsp_client = pcall(require, 'lsp_client')
    if not ok then
        log.error('failed to load lsp_client: ' .. tostring(lsp_client))
        return false
    end

    loader.lsp_client = lsp_client

    -- Resolve ydwe-lsp.exe path: same directory as the DLL
    local bin_dir = path:parent_path()
    local server_exe = bin_dir / "ydwe-lsp.exe"

    -- Fallback: check Development\AI\ydwe-lsp directory
    if not fs:exists(server_exe) then
        local dev_dir = path:parent_path():parent_path():parent_path() / "AI" / "ydwe-lsp" / "bin" / "Debug" / "Win32" / "ydwe-lsp.exe"
        if fs:exists(dev_dir) then
            server_exe = dev_dir
        end
    end

    -- Configure LSP client
    lsp_client.config({
        serverPath = server_exe:string(),
        workspacePath = "",
        logLevel = 2,  -- LOG_WARNING
    })

    -- Set message callback
    lsp_client.setMessageCallback(function(json_msg)
        local ok2, msg = pcall(function()
            return require 'common.json'.decode(json_msg)
        end)
        if not ok2 or not msg then return end

        -- Handle diagnostics
        if msg.method == "textDocument/publishDiagnostics" and msg.params then
            log.info('LSP diagnostics: ' .. tostring(msg.params.uri) .. ' ' .. tostring(#msg.params.diagnostics) .. ' items')
        end
    end)

    -- Start the LSP server
    local started = lsp_client.start()
    if not started then
        log.error('failed to start LSP server: ' .. server_exe:string())
        return false
    end

    log.info('LSP server started: ' .. server_exe:string())
    return true
end

loader.unload = function()
    if loader.lsp_client then
        loader.lsp_client.stop()
        loader.lsp_client = nil
    end
end

return loader
