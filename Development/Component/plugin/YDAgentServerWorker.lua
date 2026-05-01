local ffi = require "ffi"
local log = require "log"
require "bee"
local socket = require "bee.socket"
local thread = require "bee.thread"

local field_map = require "YDAgentFieldMap"
local ai = require "YDAgentAI"

local PORT = rawget(_G, "YDAGENT_PORT") or 27118
local DLL_PATH = rawget(_G, "YDAGENT_DLL_PATH") or "YDTrigger.dll"
local COMPONENT_ROOT = rawget(_G, "YDAGENT_COMPONENT_ROOT") or ""
local APPLY_APPROVAL_CHANNEL = "ydagent_apply_approval"
local REVIEW_CHANNEL = "ydagent_review_plan"
local START_TIME = os.time()
local LAST_ERROR = nil
local REVIEW_PUBLISHED = 0

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

local ok_ydt, loaded_ydt = pcall(ffi.load, DLL_PATH)
if not ok_ydt then
    log.error("YDAgentServer: failed to load YDTrigger.dll: " .. tostring(loaded_ydt))
    return
end

local YDT = loaded_ydt
local ok_field_map, field_map_err = pcall(field_map.load, COMPONENT_ROOT)
if not ok_field_map then
    log.error("YDAgentServer: failed to load field map: " .. tostring(field_map_err))
end

local json = {}
json.null = {}

local function clear_apply_approvals()
    local ch = thread.channel(APPLY_APPROVAL_CHANNEL)
    while true do
        local ok = ch:pop()
        if not ok then
            break
        end
    end
end

local function consume_apply_approval()
    local ok, value = thread.channel(APPLY_APPROVAL_CHANNEL):pop()
    return ok and value == "approved"
end

local function publish_review_plan(plan)
    REVIEW_PUBLISHED = REVIEW_PUBLISHED + 1
    thread.channel(REVIEW_CHANNEL):push(json.encode(plan))
end

local function clear_review_queue()
    local ch = thread.channel(REVIEW_CHANNEL)
    while true do
        local ok = ch:pop()
        if not ok then
            break
        end
    end
end

clear_apply_approvals()
clear_review_queue()

local function json_escape(s)
    return (s:gsub('[%c\\\"]', {
        ['\b'] = '\\b',
        ['\f'] = '\\f',
        ['\n'] = '\\n',
        ['\r'] = '\\r',
        ['\t'] = '\\t',
        ['\\'] = '\\\\',
        ['\"'] = '\\"',
    }))
end

function json.encode(v)
    if v == json.null then
        return "null"
    end

    local t = type(v)
    if t == "nil" then
        return "null"
    elseif t == "boolean" then
        return v and "true" or "false"
    elseif t == "number" then
        return tostring(v)
    elseif t == "string" then
        return '"' .. json_escape(v) .. '"'
    elseif t == "table" then
        local is_array = true
        local max_k = 0
        for k in pairs(v) do
            if type(k) ~= "number" or k < 1 or k ~= math.floor(k) then
                is_array = false
                break
            end
            if k > max_k then
                max_k = k
            end
        end
        if is_array then
            for i = 1, max_k do
                if v[i] == nil then
                    is_array = false
                    break
                end
            end
        end
        local parts = {}
        if is_array then
            for i = 1, max_k do
                parts[i] = json.encode(v[i])
            end
            return "[" .. table.concat(parts, ",") .. "]"
        end
        local i = 1
        for k, value in pairs(v) do
            parts[i] = '"' .. json_escape(tostring(k)) .. '":' .. json.encode(value)
            i = i + 1
        end
        return "{" .. table.concat(parts, ",") .. "}"
    end
    return "null"
end

local decode_str
local decode_pos

local function skip_ws()
    while decode_pos <= #decode_str do
        local c = decode_str:sub(decode_pos, decode_pos)
        if c ~= " " and c ~= "\t" and c ~= "\n" and c ~= "\r" then
            return
        end
        decode_pos = decode_pos + 1
    end
end

local function parse_string()
    decode_pos = decode_pos + 1
    local out = {}
    while decode_pos <= #decode_str do
        local c = decode_str:sub(decode_pos, decode_pos)
        if c == '"' then
            decode_pos = decode_pos + 1
            return table.concat(out)
        end
        if c == "\\" then
            decode_pos = decode_pos + 1
            c = decode_str:sub(decode_pos, decode_pos)
            if c == "b" then c = "\b"
            elseif c == "f" then c = "\f"
            elseif c == "n" then c = "\n"
            elseif c == "r" then c = "\r"
            elseif c == "t" then c = "\t"
            end
        end
        out[#out + 1] = c
        decode_pos = decode_pos + 1
    end
    return nil, "unterminated string"
end

local function parse_value()
    skip_ws()
    if decode_pos > #decode_str then
        return nil, "unexpected end"
    end

    local c = decode_str:sub(decode_pos, decode_pos)
    if c == "{" then
        decode_pos = decode_pos + 1
        local obj = {}
        skip_ws()
        if decode_str:sub(decode_pos, decode_pos) == "}" then
            decode_pos = decode_pos + 1
            return obj
        end
        while true do
            skip_ws()
            if decode_str:sub(decode_pos, decode_pos) ~= '"' then
                return nil, "expected object key"
            end
            local key, key_err = parse_string()
            if not key then
                return nil, key_err
            end
            skip_ws()
            if decode_str:sub(decode_pos, decode_pos) ~= ":" then
                return nil, "expected :"
            end
            decode_pos = decode_pos + 1
            local value, value_err = parse_value()
            if value_err then
                return nil, value_err
            end
            obj[key] = value
            skip_ws()
            local sep = decode_str:sub(decode_pos, decode_pos)
            if sep == "}" then
                decode_pos = decode_pos + 1
                return obj
            elseif sep == "," then
                decode_pos = decode_pos + 1
            else
                return nil, "expected , or }"
            end
        end
    elseif c == "[" then
        decode_pos = decode_pos + 1
        local arr = {}
        skip_ws()
        if decode_str:sub(decode_pos, decode_pos) == "]" then
            decode_pos = decode_pos + 1
            return arr
        end
        while true do
            local value, value_err = parse_value()
            if value_err then
                return nil, value_err
            end
            arr[#arr + 1] = value
            skip_ws()
            local sep = decode_str:sub(decode_pos, decode_pos)
            if sep == "]" then
                decode_pos = decode_pos + 1
                return arr
            elseif sep == "," then
                decode_pos = decode_pos + 1
            else
                return nil, "expected , or ]"
            end
        end
    elseif c == '"' then
        return parse_string()
    elseif c == "t" and decode_str:sub(decode_pos, decode_pos + 3) == "true" then
        decode_pos = decode_pos + 4
        return true
    elseif c == "f" and decode_str:sub(decode_pos, decode_pos + 4) == "false" then
        decode_pos = decode_pos + 5
        return false
    elseif c == "n" and decode_str:sub(decode_pos, decode_pos + 3) == "null" then
        decode_pos = decode_pos + 4
        return json.null
    elseif c == "-" or c:match("%d") then
        local s = decode_str:match("^(%-?%d+%.?%d*[eE]?[%+%-]?%d*)", decode_pos)
        if not s then
            return nil, "invalid number"
        end
        decode_pos = decode_pos + #s
        return tonumber(s)
    end
    return nil, "unexpected character: " .. c
end

function json.decode(s)
    decode_str = s or ""
    decode_pos = 1
    local value, err = parse_value()
    if err then
        return nil, err
    end
    skip_ws()
    if decode_pos <= #decode_str then
        return nil, "trailing data"
    end
    return value
end

ai.set_json(json)

local function to_str(p)
    if p == nil then
        return nil
    end
    local s = ffi.string(p)
    if s == "" then
        return nil
    end
    return s
end

local function path_join(root, name)
    if root:sub(-1) == "\\" or root:sub(-1) == "/" then
        return root .. name
    end
    return root .. "\\" .. name
end

local OBJ_TYPES = {
    unit = 0,
    item = 1,
    buff = 2,
    doodad = 3,
    ability = 4,
    hero = 5,
    upgrade = 6,
}

local OBJ_FILES = {
    "war3map.w3u",
    "war3map.w3t",
    "war3map.w3b",
    "war3map.w3d",
    "war3map.w3a",
    "war3map.w3h",
    "war3map.w3q",
}

local agent = {}
agent.EVENT = 0
agent.CONDITION = 1
agent.ACTION = 2

function agent.refresh()
    local n = YDT.ydt_refresh()
    return n > 0 and n or nil
end

function agent.trigger_count()
    return tonumber(YDT.ydt_get_trigger_count())
end

function agent.trigger_name(idx)
    return to_str(YDT.ydt_get_trigger_name(idx))
end

function agent.trigger_disabled(idx)
    local r = YDT.ydt_get_trigger_disabled(idx)
    return r >= 0 and (r ~= 0) or nil
end

function agent.eca_count(idx, eca_type)
    return tonumber(YDT.ydt_get_eca_count(idx, eca_type))
end

function agent.eca_func_name(idx, eca_type, eca_i)
    return to_str(YDT.ydt_get_eca_func_name(idx, eca_type, eca_i))
end

function agent.eca_gui_id(idx, eca_type, eca_i)
    local r = YDT.ydt_get_eca_gui_id(idx, eca_type, eca_i)
    return r >= 0 and r or nil
end

function agent.eca_param_count(idx, eca_type, eca_i)
    return tonumber(YDT.ydt_get_eca_param_count(idx, eca_type, eca_i))
end

function agent.eca_param_value(idx, eca_type, eca_i, p_idx)
    return to_str(YDT.ydt_get_eca_param_value(idx, eca_type, eca_i, p_idx))
end

function agent.set_trigger_name(idx, name)
    return YDT.ydt_set_trigger_name(idx, name) ~= 0
end

function agent.set_trigger_disabled(idx, disabled)
    return YDT.ydt_set_trigger_disabled(idx, disabled and 1 or 0) ~= 0
end

function agent.set_eca_func_name(idx, eca_type, eca_i, name)
    return YDT.ydt_set_eca_func_name(idx, eca_type, eca_i, name) ~= 0
end

function agent.set_eca_active(idx, eca_type, eca_i, active)
    return YDT.ydt_set_eca_active(idx, eca_type, eca_i, active and 1 or 0) ~= 0
end

function agent.set_eca_param_value(idx, eca_type, eca_i, p_idx, value)
    return YDT.ydt_set_eca_param_value(idx, eca_type, eca_i, p_idx, value) ~= 0
end

function agent.add_eca(idx, eca_type)
    return YDT.ydt_add_eca(idx, eca_type) ~= 0
end

function agent.remove_eca(idx, eca_type, eca_i)
    return YDT.ydt_remove_eca(idx, eca_type, eca_i) ~= 0
end

local function read_eca_list(idx, eca_type)
    local count = agent.eca_count(idx, eca_type) or 0
    local list = {}
    for i = 0, count - 1 do
        local fn = agent.eca_func_name(idx, eca_type, i)
        if fn then
            local node = {
                func = fn,
                gui_id = agent.eca_gui_id(idx, eca_type, i),
                params = {},
            }
            local pc = agent.eca_param_count(idx, eca_type, i) or 0
            for p = 0, pc - 1 do
                node.params[p + 1] = agent.eca_param_value(idx, eca_type, i, p)
            end
            list[#list + 1] = node
        end
    end
    return list
end

local ECA_NAMES = {
    [0] = "event",
    [1] = "condition",
    [2] = "action",
}

local function summarize_eca_nodes(nodes, eca_type)
    local list = {}
    for i, node in ipairs(nodes or {}) do
        list[#list + 1] = {
            index = i - 1,
            type = ECA_NAMES[eca_type],
            func = node.func,
            gui_id = node.gui_id,
            param_count = #(node.params or {}),
            params = node.params or {},
        }
    end
    return list
end

local function build_trigger_summary(idx)
    local tree = agent.get_eca_tree(idx)
    if not tree then
        return nil, "trigger not found: " .. tostring(idx)
    end

    return {
        index = idx,
        name = agent.trigger_name(idx),
        disabled = agent.trigger_disabled(idx),
        counts = {
            events = #tree.events,
            conditions = #tree.conditions,
            actions = #tree.actions,
        },
        events = summarize_eca_nodes(tree.events, 0),
        conditions = summarize_eca_nodes(tree.conditions, 1),
        actions = summarize_eca_nodes(tree.actions, 2),
    }
end

local function compact_nodes(nodes, limit)
    local compact = {}
    for i, node in ipairs(nodes or {}) do
        if i > limit then
            break
        end
        compact[#compact + 1] = {
            index = node.index,
            type = node.type,
            func = node.func,
            param_count = node.param_count,
            params = node.params,
        }
    end
    return compact
end

local function compact_trigger_summary(summary, node_limit)
    return {
        index = summary.index,
        name = summary.name,
        disabled = summary.disabled,
        counts = summary.counts,
        events = compact_nodes(summary.events, node_limit),
        conditions = compact_nodes(summary.conditions, node_limit),
        actions = compact_nodes(summary.actions, node_limit),
    }
end

local function build_context_summary(options)
    options = options or {}
    local trigger_limit = tonumber(options.trigger_limit) or 20
    local node_limit = tonumber(options.node_limit) or 20
    local count = agent.trigger_count() or 0
    local triggers = {}
    local total = {
        triggers = count,
        events = 0,
        conditions = 0,
        actions = 0,
    }

    for i = 0, math.min(count, trigger_limit) - 1 do
        local summary = build_trigger_summary(i)
        if summary then
            total.events = total.events + summary.counts.events
            total.conditions = total.conditions + summary.counts.conditions
            total.actions = total.actions + summary.counts.actions
            triggers[#triggers + 1] = compact_trigger_summary(summary, node_limit)
        end
    end

    return {
        total = total,
        included = #triggers,
        truncated = count > trigger_limit,
        trigger_limit = trigger_limit,
        node_limit = node_limit,
        triggers = triggers,
    }
end

local function collect_identifiers(value, out)
    if type(value) ~= "string" then
        return
    end
    for name in value:gmatch("[A-Za-z_][A-Za-z0-9_]*") do
        if #name >= 3 then
            out[name] = true
        end
    end
end

local function analyze_context_relationships(options)
    local context = build_context_summary(options)
    local identifiers = {}
    local trigger_refs = {}

    for _, trigger in ipairs(context.triggers or {}) do
        local refs = {}
        for _, group in ipairs({ trigger.events, trigger.conditions, trigger.actions }) do
            for _, node in ipairs(group or {}) do
                collect_identifiers(node.func, refs)
                for _, param in ipairs(node.params or {}) do
                    collect_identifiers(param, refs)
                end
            end
        end
        trigger_refs[#trigger_refs + 1] = {
            index = trigger.index,
            name = trigger.name,
            refs = refs,
        }
        for name in pairs(refs) do
            identifiers[name] = identifiers[name] or {}
            identifiers[name][#identifiers[name] + 1] = trigger.index
        end
    end

    local shared = {}
    for name, indexes in pairs(identifiers) do
        if #indexes > 1 then
            shared[#shared + 1] = {
                name = name,
                triggers = indexes,
            }
        end
    end
    table.sort(shared, function(a, b)
        if #a.triggers == #b.triggers then
            return a.name < b.name
        end
        return #a.triggers > #b.triggers
    end)

    return {
        context = context,
        shared_identifiers = shared,
        trigger_refs = trigger_refs,
    }
end

function agent.get_eca_tree(idx)
    local tc = agent.trigger_count()
    if not tc or idx < 0 or idx >= tc then
        return nil
    end
    return {
        events = read_eca_list(idx, 0),
        conditions = read_eca_list(idx, 1),
        actions = read_eca_list(idx, 2),
    }
end

function agent.list_triggers()
    local count = agent.trigger_count() or 0
    local list = {}
    for i = 0, count - 1 do
        list[#list + 1] = {
            name = agent.trigger_name(i),
            disabled = agent.trigger_disabled(i),
            event_count = agent.eca_count(i, 0),
            condition_count = agent.eca_count(i, 1),
            action_count = agent.eca_count(i, 2),
        }
    end
    return list
end

function agent.dump_all()
    local count = agent.trigger_count() or 0
    local dump = {}
    for i = 0, count - 1 do
        local item = {
            name = agent.trigger_name(i),
            disabled = agent.trigger_disabled(i),
        }
        local tree = agent.get_eca_tree(i)
        if tree then
            item.events = tree.events
            item.conditions = tree.conditions
            item.actions = tree.actions
        end
        dump[#dump + 1] = item
    end
    return dump
end

function agent.summarize_trigger(idx)
    return build_trigger_summary(idx)
end

function agent.build_prompt(idx, instruction)
    local summary, err = build_trigger_summary(idx)
    if not summary then
        return nil, err
    end

    local payload = {
        instruction = instruction or "",
        trigger = summary,
        operation_schema = ai.operation_schema(),
    }

    return table.concat({
        "You are editing a Warcraft III GUI trigger through YDWE Agent.",
        "Return JSON only. Do not include markdown fences.",
        "Return an object with fields: summary and operations.",
        "Use only operations declared in operation_schema.",
        "Do not request direct file writes or arbitrary code execution.",
        json.encode(payload),
    }, "\n")
end

function agent.compress_context(options)
    return build_context_summary(options)
end

function agent.build_batch_prompt(instruction, options)
    local context = build_context_summary(options)
    local payload = {
        instruction = instruction or "",
        context = context,
        operation_schema = ai.operation_schema(),
    }

    return table.concat({
        "You are editing Warcraft III GUI triggers through YDWE Agent.",
        "Return JSON only. Do not include markdown fences.",
        "Return an object with fields: summary and operations.",
        "Use only operations declared in operation_schema.",
        "Prefer minimal safe changes. Do not request arbitrary code execution.",
        json.encode(payload),
    }, "\n")
end

function agent.analyze_relationships(options)
    return analyze_context_relationships(options)
end

function agent.build_relationship_prompt(instruction, options)
    local analysis = analyze_context_relationships(options)
    local payload = {
        instruction = instruction or "",
        relationships = analysis,
        operation_schema = ai.operation_schema(),
    }
    return table.concat({
        "Analyze Warcraft III GUI trigger relationships through YDWE Agent.",
        "Return JSON only if edits are suggested; otherwise return concise Chinese explanation.",
        "Use shared_identifiers to find likely variable or function coupling.",
        json.encode(payload),
    }, "\n")
end

local object = {}

function object.read(type_name, map_path)
    local ot = OBJ_TYPES[type_name]
    if not ot then
        return nil, "unknown object type: " .. tostring(type_name)
    end
    local p = YDT.ydt_read_object_file(path_join(map_path, OBJ_FILES[ot + 1]))
    return to_str(p)
end

local function annotate_records(records, source)
    for _, record in ipairs(records or {}) do
        record.fieldInfo = field_map.annotate_fields(record.fields or {}, source)
    end
end

function object.read_annotated(type_name, map_path)
    local data, err = object.read(type_name, map_path)
    if not data then
        return nil, err
    end

    local decoded, parse_err = json.decode(data)
    if parse_err then
        return nil, "failed to parse object json: " .. tostring(parse_err)
    end

    decoded.fieldMapSource = type_name
    annotate_records(decoded.original, type_name)
    annotate_records(decoded.custom, type_name)
    return decoded
end

function object.write(type_name, map_path, json_data)
    local ot = OBJ_TYPES[type_name]
    if not ot then
        return nil, "unknown object type: " .. tostring(type_name)
    end
    return YDT.ydt_write_object_file(path_join(map_path, OBJ_FILES[ot + 1]), json_data) ~= 0
end

function object.field_name(field_id, source)
    return field_map.name(field_id, source)
end

function object.field_info(field_id, source)
    return field_map.get(field_id, source)
end

function object.field_map(source)
    return field_map.summary(source)
end

function object.types()
    local list = {}
    for name, id in pairs(OBJ_TYPES) do
        list[#list + 1] = {
            name = name,
            id = id,
            file = OBJ_FILES[id + 1],
        }
    end
    table.sort(list, function(a, b)
        return a.id < b.id
    end)
    return list
end

local function find_record(records, object_id)
    for _, record in ipairs(records or {}) do
        if record.id == object_id then
            return record
        end
    end
    return nil
end

local function apply_object_set_field(op, options)
    local map_path = options and options.map_path
    if type(map_path) ~= "string" or map_path == "" then
        return false, "map_path is required for object_set_field"
    end

    local data, err = object.read(op.type_name, map_path)
    if not data then
        return false, err or "failed to read object data"
    end

    local decoded, parse_err = json.decode(data)
    if parse_err then
        return false, "failed to parse object json: " .. tostring(parse_err)
    end

    local records = decoded[op.record_kind]
    local record = find_record(records, op.object_id)
    if not record then
        return false, "object record not found: " .. tostring(op.object_id)
    end

    record.fields = record.fields or {}
    record.fields[op.field_id] = op.value

    local ok = object.write(op.type_name, map_path, json.encode(decoded))
    if not ok then
        return false, "failed to write object data"
    end
    return true
end

local function apply_operation(op, options)
    if op.op == "set_trigger_name" then
        return agent.set_trigger_name(op.trigger_index, op.name)
    elseif op.op == "set_trigger_disabled" then
        return agent.set_trigger_disabled(op.trigger_index, op.disabled)
    elseif op.op == "set_eca_func_name" then
        return agent.set_eca_func_name(op.trigger_index, op.eca_type, op.eca_index, op.func)
    elseif op.op == "set_eca_active" then
        return agent.set_eca_active(op.trigger_index, op.eca_type, op.eca_index, op.active)
    elseif op.op == "set_eca_param_value" then
        return agent.set_eca_param_value(op.trigger_index, op.eca_type, op.eca_index, op.param_index, op.value)
    elseif op.op == "add_eca" then
        local new_index = agent.eca_count(op.trigger_index, op.eca_type) or 0
        local ok = agent.add_eca(op.trigger_index, op.eca_type)
        if ok and op.func then
            ok = agent.set_eca_func_name(op.trigger_index, op.eca_type, new_index, op.func)
        end
        return ok
    elseif op.op == "remove_eca" then
        return agent.remove_eca(op.trigger_index, op.eca_type, op.eca_index)
    elseif op.op == "object_set_field" then
        return apply_object_set_field(op, options)
    end
    return false, "unsupported operation: " .. tostring(op.op)
end

local function apply_plan(plan, options)
    options = options or {}
    local source_plan = type(plan) == "table" and plan.plan ~= nil and plan.plan or plan
    local validation = ai.validate_plan(source_plan)
    local result = {
        ok = false,
        dry_run = options.dry_run ~= false,
        applied = {},
        validation = validation,
    }

    if not validation.ok then
        result.error = "plan validation failed"
        return result
    end

    if result.dry_run then
        result.ok = true
        result.pending = validation.operations
        return result
    end

    if options.confirm ~= true then
        result.error = "confirm=true is required to apply operations"
        return result
    end

    if not consume_apply_approval() then
        result.error = "UI approval is required before applying operations"
        return result
    end

    for index, op in ipairs(validation.operations) do
        local ok, err = apply_operation(op, options)
        result.applied[#result.applied + 1] = {
            index = index,
            op = op.op,
            ok = ok == true,
            error = ok == true and nil or (err or "operation returned false"),
        }
        if ok ~= true and options.continue_on_error ~= true then
            result.error = err or "operation failed"
            return result
        end
    end

    result.ok = true
    return result
end

local ai_rpc = {}

function ai_rpc.configure(provider, options)
    local result, err = ai.configure(provider, options)
    if not result then
        return nil, err
    end
    return result
end

function ai_rpc.status()
    return ai.status()
end

function ai_rpc.build_request(prompt, context, options)
    return ai.build(prompt, context, options)
end

function ai_rpc.complete(prompt, context, options)
    local result, err = ai.complete(prompt, context, options)
    if not result then
        return {
            ok = false,
            error = err,
        }
    end
    return {
        ok = true,
        result = result,
    }
end

function ai_rpc.parse_response(provider, response_text)
    local result, err = ai.parse_response(provider, response_text)
    if not result then
        return nil, err
    end
    return result
end

function ai_rpc.validate_plan(plan)
    return ai.validate_plan(plan)
end

function ai_rpc.operation_schema()
    return ai.operation_schema()
end

function ai_rpc.apply_plan(plan, options)
    return apply_plan(plan, options)
end

function ai_rpc.queue_review(plan)
    local source_plan = type(plan) == "table" and plan.plan ~= nil and plan.plan or plan
    local payload = {
        provider = type(plan) == "table" and plan.provider or "manual",
        text = type(plan) == "table" and plan.text or nil,
        plan = source_plan,
        validation = ai.validate_plan(source_plan),
    }
    publish_review_plan(payload)
    return {
        ok = true,
        validation = payload.validation,
    }
end

function ai_rpc.generate_plan(prompt, context, options)
    options = options or {}
    local result, err = ai.generate_plan(prompt, context, options)
    if not result then
        return nil, err
    end
    if options.queue_review ~= false then
        publish_review_plan(result)
    end
    return result
end

function ai_rpc.generate_trigger_plan(idx, instruction, options)
    local prompt, err = agent.build_prompt(idx, instruction)
    if not prompt then
        return nil, err
    end
    return ai_rpc.generate_plan(prompt, nil, options)
end

function ai_rpc.generate_batch_plan(instruction, context_options, options)
    local prompt = agent.build_batch_prompt(instruction, context_options)
    return ai_rpc.generate_plan(prompt, nil, options)
end

function ai_rpc.generate_relationship_plan(instruction, context_options, options)
    local prompt = agent.build_relationship_prompt(instruction, context_options)
    return ai_rpc.generate_plan(prompt, nil, options)
end

function ai_rpc.generate_text(prompt, context, options)
    local result, err = ai.generate_text(prompt, context, options)
    if not result then
        return nil, err
    end
    return result
end

function ai_rpc.generate_code(language, instruction, context, options)
    language = language or "jass"
    local prompt = table.concat({
        "Generate Warcraft III " .. tostring(language) .. " code for YDWE.",
        "Return code only unless explanation is explicitly requested.",
        "Avoid unsafe file operations and external process execution.",
        tostring(instruction or ""),
    }, "\n")
    return ai_rpc.generate_text(prompt, context, options)
end

function ai_rpc.explain_trigger(idx, options)
    local summary, err = build_trigger_summary(idx)
    if not summary then
        return nil, err
    end
    local prompt = table.concat({
        "Explain this Warcraft III GUI trigger in Chinese.",
        "Summarize events, conditions, actions, and possible risks.",
        json.encode(summary),
    }, "\n")
    return ai_rpc.generate_text(prompt, nil, options)
end

local diag = {}

function diag.status()
    local trigger_count = agent.trigger_count() or 0
    return {
        ok = true,
        port = PORT,
        uptime = os.time() - START_TIME,
        component_root = COMPONENT_ROOT,
        dll_path = DLL_PATH,
        field_map_loaded = ok_field_map == true,
        trigger_count = trigger_count,
        review_published = REVIEW_PUBLISHED,
        ai = ai.status(),
        last_error = LAST_ERROR,
    }
end

function diag.smoke()
    local checks = {}
    local function check(name, fn)
        local ok, result = pcall(fn)
        checks[#checks + 1] = {
            name = name,
            ok = ok and result ~= nil,
            result = ok and result or nil,
            error = ok and nil or tostring(result),
        }
    end

    check("diag.status", function()
        return diag.status()
    end)
    check("agent.list_triggers", function()
        return agent.list_triggers()
    end)
    check("agent.compress_context", function()
        return agent.compress_context({ trigger_limit = 3, node_limit = 3 })
    end)
    check("ai.operation_schema", function()
        return ai.operation_schema()
    end)
    check("ai.validate_plan", function()
        return ai.validate_plan({
            operations = {
                { op = "set_trigger_disabled", trigger_index = 0, disabled = false },
            },
        })
    end)
    check("ai.apply_plan_dry_run", function()
        return apply_plan({
            operations = {
                { op = "set_trigger_disabled", trigger_index = 0, disabled = false },
            },
        }, { dry_run = true })
    end)

    local ok = true
    for _, item in ipairs(checks) do
        if not item.ok then
            ok = false
            break
        end
    end
    return {
        ok = ok,
        checks = checks,
    }
end

local function dispatch(method, params)
    local namespace, fn_name = method:match("^([%w_]+)%.([%w_]+)$")
    if not namespace then
        return nil, -32601, "Method not found: " .. tostring(method)
    end

    local target = namespace == "agent" and agent or namespace == "object" and object or namespace == "ai" and ai_rpc or namespace == "diag" and diag or nil
    if not target then
        return nil, -32601, "Method not found: " .. tostring(method)
    end

    local fn = target[fn_name]
    if type(fn) ~= "function" then
        return nil, -32601, "Method not found: " .. tostring(method)
    end

    local ok, result, err
    if type(params) == "table" and #params > 0 then
        ok, result, err = pcall(fn, table.unpack(params))
    elseif type(params) == "table" and next(params) == nil then
        ok, result, err = pcall(fn)
    elseif type(params) == "table" then
        return nil, -32602, "Named params are not supported; use positional params"
    else
        return nil, -32602, "Invalid params"
    end
    if not ok then
        LAST_ERROR = tostring(result)
        return nil, -32603, tostring(result)
    end
    if err then
        LAST_ERROR = tostring(err)
        return nil, -32602, tostring(err)
    end
    return result
end

local function handle_request(data)
    local req, parse_err = json.decode(data or "")
    if type(req) ~= "table" then
        return json.encode({
            jsonrpc = "2.0",
            id = json.null,
            error = { code = -32700, message = parse_err or "Parse error" },
        })
    end

    if type(req.method) ~= "string" then
        return json.encode({
            jsonrpc = "2.0",
            id = req.id ~= nil and req.id or json.null,
            error = { code = -32600, message = "Invalid Request" },
        })
    end

    local params = req.params
    if params == nil or params == json.null then
        params = {}
    end

    local result, code, message = dispatch(req.method, params)
    if code then
        return json.encode({
            jsonrpc = "2.0",
            id = req.id ~= nil and req.id or json.null,
            error = { code = code, message = message },
        })
    end

    return json.encode({
        jsonrpc = "2.0",
        id = req.id ~= nil and req.id or json.null,
        result = result ~= nil and result or json.null,
    })
end

local function send_all(client, data)
    local offset = 1
    while offset <= #data do
        local _, writable = socket.select(nil, { client }, 1)
        if not writable or not writable[1] then
            return false, "send timeout"
        end
        local n = client:send(data:sub(offset))
        if not n then
            return false, "send failed"
        end
        offset = offset + n
    end
    return true
end

local function read_request(client)
    local data = ""
    local wait_count = 0
    while #data < 1048576 do
        local readable = socket.select({ client }, nil, 0.1)
        if not readable or not readable[1] then
            wait_count = wait_count + 1
            if wait_count >= 25 then
                break
            end
        else
            local chunk = client:recv(8192)
            if chunk == nil then
                break
            end
            if chunk ~= false then
                data = data .. chunk
                local _, parse_err = json.decode(data)
                if not parse_err then
                    break
                end
                wait_count = 0
            end
        end
    end
    return data
end

local server, bind_err = socket.bind("tcp", "127.0.0.1", PORT)
if not server then
    log.error("YDAgentServer: bind failed: " .. tostring(bind_err))
    return
end

local stop_channel = thread.channel("ydagent_stop")
log.info("YDAgentServer: listening on 127.0.0.1:" .. tostring(PORT))

while true do
    local stop_ok, stop_msg = stop_channel:pop()
    if stop_ok and stop_msg == "stop" then
        break
    end

    local readable = socket.select({ server }, nil, 0.1)
    if readable and readable[1] then
        local client = server:accept()
        if client then
            local ok, err = pcall(function()
                local data = read_request(client)
                if #data > 0 then
                    send_all(client, handle_request(data))
                end
            end)
            if not ok then
                log.error("YDAgentServer: request failed: " .. tostring(err))
            end
            client:close()
        end
    end
end

server:close()
log.info("YDAgentServer: stopped")
