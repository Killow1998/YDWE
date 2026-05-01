-- YDWE AI Agent Core Plugin
-- Loads YDTrigger.dll and exposes its Agent API (C exports) to Lua via FFI.
-- Provides Lua-level API for reading GUI triggers, ECA nodes, and parameters.
-- This is the bridge between AI Agent (external process) and WE's internal trigger data.
--
-- Usage from other Lua plugins:
--   local agent = require "YDAgentCore"
--   agent.refresh()
--   local triggers = agent.list_triggers()
--   local tree = agent.get_eca_tree(0)

local ffi = require "ffi"
local log = require "log"

ffi.cdef[[
    int  ydt_refresh(void);
    int  ydt_get_trigger_count(void);
    const char* ydt_get_trigger_name(int trig_index);
    int  ydt_get_trigger_disabled(int trig_index);
    int  ydt_get_eca_count(int trig_index, int eca_type);
    const char* ydt_get_eca_func_name(int trig_index, int eca_type, int eca_idx);
    int  ydt_get_eca_gui_id(int trig_index, int eca_type, int eca_idx);
    int  ydt_get_eca_param_count(int trig_index, int eca_type, int eca_idx);
    const char* ydt_get_eca_param_value(int trig_index, int eca_type, int eca_idx, int param_idx);

    int  ydt_set_trigger_name(int trig_index, const char* name);
    int  ydt_set_trigger_disabled(int trig_index, int disabled);
    int  ydt_set_eca_func_name(int trig_index, int eca_type, int eca_idx, const char* name);
    int  ydt_set_eca_active(int trig_index, int eca_type, int eca_idx, int active);
    int  ydt_set_eca_param_value(int trig_index, int eca_type, int eca_idx, int param_idx, const char* value);
    int  ydt_add_eca(int trig_index, int eca_type);
    int  ydt_remove_eca(int trig_index, int eca_type, int eca_idx);

    const char* ydt_read_object_file(const char* file_path);
    int  ydt_write_object_file(const char* file_path, const char* json_data);
]]

----------------------------------------------------------------------
-- Agent API wrapper
----------------------------------------------------------------------

local agent = {}

-- ECA type constants
agent.EVENT     = 0
agent.CONDITION = 1
agent.ACTION    = 2

-- Internal: the loaded DLL
local YDT = nil
local field_map = nil

-- Safely convert a const char* from FFI to Lua string (or nil)
local function to_str(p)
    if p == nil then return nil end
    local s = ffi.string(p)
    if s == "" then return nil end
    return s
end

----------------------------------------------------------------------
-- Core wrappers (thin wrappers over C exports, handles nil safely)
----------------------------------------------------------------------

--- Refresh trigger cache from WE memory. Must be called before reading.
--- @return number|nil trigger count, or nil if failed
function agent.refresh()
    if not YDT then return nil end
    local n = YDT.ydt_refresh()
    return n > 0 and n or nil
end

--- Get cached trigger count (after refresh).
--- @return number
function agent.trigger_count()
    if not YDT then return 0 end
    return tonumber(YDT.ydt_get_trigger_count())
end

--- Get trigger name by index.
--- @param trig_index number  0-based trigger index
--- @return string|nil
function agent.trigger_name(trig_index)
    if not YDT then return nil end
    return to_str(YDT.ydt_get_trigger_name(trig_index))
end

--- Check if trigger is disabled.
--- @param trig_index number  0-based trigger index
--- @return boolean|nil  true=disabled, false=enabled, nil=error
function agent.trigger_disabled(trig_index)
    if not YDT then return nil end
    local r = YDT.ydt_get_trigger_disabled(trig_index)
    if r < 0 then return nil end
    return r ~= 0
end

--- Get ECA node count for a trigger.
--- @param trig_index number  0-based trigger index
--- @param eca_type number  0=Event, 1=Condition, 2=Action
--- @return number
function agent.eca_count(trig_index, eca_type)
    if not YDT then return 0 end
    return tonumber(YDT.ydt_get_eca_count(trig_index, eca_type))
end

--- Get ECA node function name.
--- @param trig_index number
--- @param eca_type number
--- @param eca_idx number  0-based index within eca_type
--- @return string|nil
function agent.eca_func_name(trig_index, eca_type, eca_idx)
    if not YDT then return nil end
    return to_str(YDT.ydt_get_eca_func_name(trig_index, eca_type, eca_idx))
end

--- Get ECA node GUI ID.
--- @return number|nil  GUIID enum value, nil on error
function agent.eca_gui_id(trig_index, eca_type, eca_idx)
    if not YDT then return nil end
    local r = YDT.ydt_get_eca_gui_id(trig_index, eca_type, eca_idx)
    return r >= 0 and r or nil
end

--- Get ECA node parameter count.
--- @return number
function agent.eca_param_count(trig_index, eca_type, eca_idx)
    if not YDT then return 0 end
    return tonumber(YDT.ydt_get_eca_param_count(trig_index, eca_type, eca_idx))
end

--- Get ECA node parameter value.
--- @param param_idx number  0-based parameter index
--- @return string|nil
function agent.eca_param_value(trig_index, eca_type, eca_idx, param_idx)
    if not YDT then return nil end
    return to_str(YDT.ydt_get_eca_param_value(trig_index, eca_type, eca_idx, param_idx))
end

----------------------------------------------------------------------
-- High-level convenience functions
----------------------------------------------------------------------

--- Read all ECA nodes of a given type for a trigger.
--- Returns an array of ECA node tables: { {func, gui_id, params={...}}, ... }
--- @param trig_index number
--- @param eca_type number
--- @param eca_type_name string  used for error reporting only
--- @return table
local function read_eca_list(trig_index, eca_type)
    local count = agent.eca_count(trig_index, eca_type)
    if count == 0 then return {} end
    local list = {}
    for i = 0, count - 1 do
        local func_name = agent.eca_func_name(trig_index, eca_type, i)
        if func_name then
            local node = {
                func    = func_name,
                gui_id  = agent.eca_gui_id(trig_index, eca_type, i),
                params  = {},
            }
            local pcount = agent.eca_param_count(trig_index, eca_type, i)
            for p = 0, pcount - 1 do
                node.params[p + 1] = agent.eca_param_value(trig_index, eca_type, i, p)
            end
            list[i + 1] = node
        end
    end
    return list
end

--- Get complete ECA tree for a trigger.
--- Returns: { events = {...}, conditions = {...}, actions = {...} }
--- @param trig_index number
--- @return table|nil
function agent.get_eca_tree(trig_index)
    if not YDT then return nil end
    if trig_index < 0 or trig_index >= agent.trigger_count() then return nil end
    return {
        events     = read_eca_list(trig_index, agent.EVENT),
        conditions = read_eca_list(trig_index, agent.CONDITION),
        actions    = read_eca_list(trig_index, agent.ACTION),
    }
end

--- List all triggers with basic info.
--- Returns: { {name, disabled, event_count, condition_count, action_count}, ... }
--- @return table
function agent.list_triggers()
    if not YDT then return {} end
    local count = agent.trigger_count()
    local list = {}
    for i = 0, count - 1 do
        list[i + 1] = {
            name           = agent.trigger_name(i),
            disabled       = agent.trigger_disabled(i),
            event_count    = agent.eca_count(i, agent.EVENT),
            condition_count = agent.eca_count(i, agent.CONDITION),
            action_count   = agent.eca_count(i, agent.ACTION),
        }
    end
    return list
end

--- Dump ALL triggers with full ECA trees.
--- Returns a large table: { {name, disabled, events={...}, conditions={...}, actions={...}}, ... }
--- @return table
function agent.dump_all()
    if not YDT then return {} end
    local count = agent.trigger_count()
    local dump = {}
    for i = 0, count - 1 do
        local entry = {
            name     = agent.trigger_name(i),
            disabled = agent.trigger_disabled(i),
        }
        local tree = agent.get_eca_tree(i)
        if tree then
            entry.events     = tree.events
            entry.conditions = tree.conditions
            entry.actions    = tree.actions
        end
        dump[i + 1] = entry
    end
    return dump
end

----------------------------------------------------------------------
-- Modification API wrappers (Phase 2)
----------------------------------------------------------------------

--- Set trigger display name.
--- @param trig_index number
--- @param name string
--- @return boolean  true on success
function agent.set_trigger_name(trig_index, name)
    return YDT and YDT.ydt_set_trigger_name(trig_index, name) ~= 0 or false
end

--- Enable or disable a trigger.
--- @param trig_index number
--- @param disabled boolean  true=disabled, false=enabled
--- @return boolean
function agent.set_trigger_disabled(trig_index, disabled)
    return YDT and YDT.ydt_set_trigger_disabled(trig_index, disabled and 1 or 0) ~= 0 or false
end

--- Change an ECA node's function name (e.g. "SetVariable", "IfThenElseMultiple").
--- @param trig_index number
--- @param eca_type number  0=Event, 1=Condition, 2=Action
--- @param eca_idx number
--- @param name string
--- @return boolean
function agent.set_eca_func_name(trig_index, eca_type, eca_idx, name)
    return YDT and YDT.ydt_set_eca_func_name(trig_index, eca_type, eca_idx, name) ~= 0 or false
end

--- Set ECA node active/inactive. Inactive nodes are skipped during compilation.
--- @param trig_index number
--- @param eca_type number
--- @param eca_idx number
--- @param active boolean
--- @return boolean
function agent.set_eca_active(trig_index, eca_type, eca_idx, active)
    return YDT and YDT.ydt_set_eca_active(trig_index, eca_type, eca_idx, active and 1 or 0) ~= 0 or false
end

--- Set an ECA node's parameter value.
--- @param trig_index number
--- @param eca_type number
--- @param eca_idx number
--- @param param_idx number
--- @param value string
--- @return boolean
function agent.set_eca_param_value(trig_index, eca_type, eca_idx, param_idx, value)
    return YDT and YDT.ydt_set_eca_param_value(trig_index, eca_type, eca_idx, param_idx, value) ~= 0 or false
end

--- Add a new ECA node to a trigger (cloned from existing template of same type).
--- @param trig_index number
--- @param eca_type number  0=Event, 1=Condition, 2=Action
--- @return boolean
function agent.add_eca(trig_index, eca_type)
    return YDT and YDT.ydt_add_eca(trig_index, eca_type) ~= 0 or false
end

--- Remove an ECA node from a trigger and free its memory.
--- @param trig_index number
--- @param eca_type number
--- @param eca_idx number
--- @return boolean
function agent.remove_eca(trig_index, eca_type, eca_idx)
    return YDT and YDT.ydt_remove_eca(trig_index, eca_type, eca_idx) ~= 0 or false
end

----------------------------------------------------------------------
-- Object Editor API wrappers
----------------------------------------------------------------------

local OBJ_FILES = {
    "war3map.w3u", "war3map.w3t", "war3map.w3b",
    "war3map.w3d", "war3map.w3a", "war3map.w3h", "war3map.w3q"
}

-- Read object data from a w3u/w3a/etc file as JSON string.
-- @param obj_type number  0=unit, 1=item, 2=buff, 3=doodad, 4=ability, 5=hero, 6=upgrade
-- @param map_path string   path to the map directory (e.g. "C:\\maps\\MyMap")
-- @return string|nil  JSON representation of the object data
function agent.object_read(obj_type, map_path)
    if not YDT then return nil end
    if obj_type < 0 or obj_type > 6 then return nil end
    local file_path = map_path .. "\\" .. OBJ_FILES[obj_type + 1]
    return to_str(YDT.ydt_read_object_file(file_path))
end

-- Write JSON object data back to a binary file.
-- @param obj_type number  0=unit, 1=item, etc.
-- @param map_path string
-- @param json_data string  JSON object data (from object_read, modified)
-- @return boolean
function agent.object_write(obj_type, map_path, json_data)
    if not YDT then return false end
    if obj_type < 0 or obj_type > 6 then return false end
    local file_path = map_path .. "\\" .. OBJ_FILES[obj_type + 1]
    return YDT.ydt_write_object_file(file_path, json_data) ~= 0
end

function agent.object_field_name(field_id, source)
    if not field_map then return field_id end
    return field_map.name(field_id, source)
end

function agent.object_field_info(field_id, source)
    if not field_map then return nil end
    return field_map.get(field_id, source)
end

function agent.object_field_map(source)
    if not field_map then return {} end
    return field_map.summary(source)
end

----------------------------------------------------------------------
-- Plugin loader interface
----------------------------------------------------------------------

local loader = {}

loader.load = function(path)
    local component_root = path:parent_path():parent_path()
    local plugin_dir = component_root / "plugin"
    package.path = plugin_dir:string() .. "\\?.lua;" .. package.path

    local ok, dll = pcall(ffi.load, path:string())
    if not ok then
        log.error('YDAgentCore: failed to load YDTrigger.dll: ' .. tostring(dll))
        return false
    end

    YDT = dll
    local ok_map, map = pcall(require, "YDAgentFieldMap")
    if ok_map then
        field_map = map
        field_map.load(component_root:string())
    else
        log.error("YDAgentCore: failed to load field map: " .. tostring(map))
    end
    _G.ydwe_agent = agent
    log.info('YDAgentCore: YDTrigger.dll loaded, Agent API available via ydwe_agent')
    return true
end

loader.unload = function()
    YDT = nil
    field_map = nil
    _G.ydwe_agent = nil
end

return loader
