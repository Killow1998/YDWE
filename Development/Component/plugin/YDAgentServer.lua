local log = require "log"
require "bee"
local thread = require "bee.thread"

local DEFAULT_PORT = 27118
local HOST = "127.0.0.1"

local loader = {}
local server_thread = nil

local function quote(value)
    return string.format("%q", tostring(value or ""))
end

local function make_thread_code(dll_path, plugin_dir, component_root, port)
    return table.concat({
        "package.path = " .. quote(plugin_dir .. "\\?.lua") .. " .. ';' .. package.path",
        "package.cpath = " .. quote(component_root .. "\\bin\\?.dll;" .. plugin_dir .. "\\?.dll") .. " .. ';' .. package.cpath",
        "require 'bee'",
        "_G.YDAGENT_DLL_PATH = " .. quote(dll_path),
        "_G.YDAGENT_COMPONENT_ROOT = " .. quote(component_root),
        "_G.YDAGENT_PORT = " .. tostring(port),
        "require 'YDAgentServerWorker'",
    }, "\n")
end

local function clear_stop_channel()
    local ch = thread.channel("ydagent_stop")
    while true do
        local ok = ch:pop()
        if not ok then
            break
        end
    end
end

loader.load = function(path)
    if server_thread then
        return true
    end

    local component_root = path:parent_path():parent_path()
    local plugin_dir = component_root / "plugin"
    local thread_code = make_thread_code(path:string(), plugin_dir:string(), component_root:string(), DEFAULT_PORT)

    clear_stop_channel()
    local ok, result = pcall(thread.thread, thread_code)
    if not ok then
        log.error("YDAgentServer: failed to start thread: " .. tostring(result))
        return false
    end

    server_thread = result
    log.info("YDAgentServer: JSON-RPC server started on " .. HOST .. ":" .. DEFAULT_PORT)
    return true
end

loader.unload = function()
    if server_thread then
        thread.channel("ydagent_stop"):push("stop")
        server_thread = nil
    end
    log.info("YDAgentServer: unloaded")
end

return loader
