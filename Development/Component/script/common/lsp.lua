-- YDWE LSP Client Integration Script
-- Usage: require 'lsp_client' in YDWE Lua environment

local lsp = require 'lsp_client'

local M = {}
M.running = false
M.serverPath = ""
M.documents = {}

-- Configure and start LSP server
function M.start(server_exe_path, workspace_path)
    if M.running then
        return true
    end

    M.serverPath = server_exe_path or ""

    lsp.config({
        serverPath = M.serverPath,
        workspacePath = workspace_path or "",
        logLevel = lsp.LOG_WARNING
    })

    lsp.setMessageCallback(function(json_msg)
        M.onMessage(json_msg)
    end)

    local ok = lsp.start()
    if ok then
        M.running = true
    end
    return ok
end

-- Stop LSP server
function M.stop()
    if M.running then
        lsp.stop()
        M.running = false
        M.documents = {}
    end
end

-- Open a document for tracking
function M.openDocument(uri, languageId, text)
    if not M.running then return false end
    M.documents[uri] = {
        languageId = languageId,
        version = 1
    }
    return lsp.openDocument(uri, languageId, text)
end

-- Update document content
function M.changeDocument(uri, startLine, startChar, endLine, endChar, newText)
    if not M.running then return false end
    if M.documents[uri] then
        M.documents[uri].version = M.documents[uri].version + 1
    end
    return lsp.changeDocument(uri, startLine, startChar, endLine, endChar, newText)
end

-- Close document
function M.closeDocument(uri)
    if not M.running then return false end
    M.documents[uri] = nil
    return lsp.closeDocument(uri)
end

-- Save document
function M.saveDocument(uri)
    if not M.running then return false end
    return lsp.saveDocument(uri)
end

-- Request completion at position
function M.requestCompletion(uri, line, character, callback)
    if not M.running then return false end
    M._completionCallback = callback
    return lsp.requestCompletion(uri, line, character, 1)
end

-- Poll for incoming messages
function M.poll()
    if M.running then
        lsp.poll()
    end
end

-- Internal message handler
function M.onMessage(json_msg)
    local ok, msg = pcall(function()
        return require 'common.json'.decode(json_msg)
    end)
    if not ok or not msg then
        return
    end

    -- Handle completion response
    if msg.id and msg.result then
        if M._completionCallback then
            M._completionCallback(msg.result)
            M._completionCallback = nil
        end
    end

    -- Handle diagnostics notification
    if msg.method == "textDocument/publishDiagnostics" then
        if M._diagnosticsCallback then
            M._diagnosticsCallback(msg.params)
        end
    end
end

-- Set diagnostics callback
function M.setDiagnosticsCallback(callback)
    M._diagnosticsCallback = callback
end

-- Check if running
function M.isRunning()
    return M.running
end

return M
