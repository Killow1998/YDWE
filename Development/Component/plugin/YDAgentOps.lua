local M = {}

local ECA_TYPES = {
    [0] = "event",
    [1] = "condition",
    [2] = "action",
}

local OBJECT_TYPES = {
    unit = true,
    item = true,
    buff = true,
    doodad = true,
    ability = true,
    hero = true,
    upgrade = true,
}

local function is_integer(value, min_value)
    return type(value) == "number" and value == math.floor(value) and value >= (min_value or 0)
end

local function is_bool(value)
    return type(value) == "boolean"
end

local function is_string(value, max_len)
    return type(value) == "string" and value ~= "" and #value <= (max_len or 4096)
end

local function is_fourcc(value)
    return type(value) == "string" and #value == 4
end

local function primitive(value)
    local t = type(value)
    return t == "string" or t == "number" or t == "boolean"
end

local function field(op, names)
    for _, name in ipairs(names) do
        if op[name] ~= nil then
            return op[name]
        end
    end
    return nil
end

local function add_error(errors, index, message)
    errors[#errors + 1] = {
        index = index,
        message = message,
    }
end

local function add_warning(warnings, index, message)
    warnings[#warnings + 1] = {
        index = index,
        message = message,
    }
end

local function base(action, risk, op)
    return {
        op = action,
        risk = risk,
        reason = type(op.reason) == "string" and op.reason or nil,
    }
end

local function validate_trigger_index(cleaned, op)
    local idx = field(op, { "trigger_index", "trigger", "triggerIndex" })
    if not is_integer(idx, 0) then
        return nil, "trigger_index must be a non-negative integer"
    end
    cleaned.trigger_index = idx
    return true
end

local function validate_eca(cleaned, op)
    local eca_type = field(op, { "eca_type", "ecaType", "kind" })
    if type(eca_type) == "string" then
        for id, name in pairs(ECA_TYPES) do
            if eca_type == name then
                eca_type = id
                break
            end
        end
    end
    if not ECA_TYPES[eca_type] then
        return nil, "eca_type must be 0/event, 1/condition, or 2/action"
    end
    cleaned.eca_type = eca_type
    return true
end

local function validate_eca_index(cleaned, op)
    local idx = field(op, { "eca_index", "eca", "ecaIndex" })
    if not is_integer(idx, 0) then
        return nil, "eca_index must be a non-negative integer"
    end
    cleaned.eca_index = idx
    return true
end

local validators = {}

validators.set_trigger_name = function(op)
    local cleaned = base("set_trigger_name", "low", op)
    local ok, err = validate_trigger_index(cleaned, op)
    if not ok then return nil, err end
    local name = field(op, { "name", "value" })
    if not is_string(name, 256) then
        return nil, "name must be a non-empty string up to 256 bytes"
    end
    cleaned.name = name
    return cleaned
end

validators.set_trigger_disabled = function(op)
    local cleaned = base("set_trigger_disabled", "low", op)
    local ok, err = validate_trigger_index(cleaned, op)
    if not ok then return nil, err end
    local disabled = field(op, { "disabled", "value" })
    if not is_bool(disabled) then
        return nil, "disabled must be boolean"
    end
    cleaned.disabled = disabled
    return cleaned
end

validators.set_eca_func_name = function(op)
    local cleaned = base("set_eca_func_name", "medium", op)
    local ok, err = validate_trigger_index(cleaned, op)
    if not ok then return nil, err end
    ok, err = validate_eca(cleaned, op)
    if not ok then return nil, err end
    ok, err = validate_eca_index(cleaned, op)
    if not ok then return nil, err end
    local name = field(op, { "func", "func_name", "funcName", "name", "value" })
    if not is_string(name, 256) then
        return nil, "func name must be a non-empty string up to 256 bytes"
    end
    cleaned.func = name
    return cleaned
end

validators.set_eca_active = function(op)
    local cleaned = base("set_eca_active", "low", op)
    local ok, err = validate_trigger_index(cleaned, op)
    if not ok then return nil, err end
    ok, err = validate_eca(cleaned, op)
    if not ok then return nil, err end
    ok, err = validate_eca_index(cleaned, op)
    if not ok then return nil, err end
    local active = field(op, { "active", "value" })
    if not is_bool(active) then
        return nil, "active must be boolean"
    end
    cleaned.active = active
    return cleaned
end

validators.set_eca_param_value = function(op)
    local cleaned = base("set_eca_param_value", "medium", op)
    local ok, err = validate_trigger_index(cleaned, op)
    if not ok then return nil, err end
    ok, err = validate_eca(cleaned, op)
    if not ok then return nil, err end
    ok, err = validate_eca_index(cleaned, op)
    if not ok then return nil, err end
    local param_idx = field(op, { "param_index", "param", "paramIndex" })
    if not is_integer(param_idx, 0) then
        return nil, "param_index must be a non-negative integer"
    end
    local value = field(op, { "value", "param_value", "paramValue" })
    if not primitive(value) then
        return nil, "value must be string, number, or boolean"
    end
    cleaned.param_index = param_idx
    cleaned.value = tostring(value)
    return cleaned
end

validators.add_eca = function(op)
    local cleaned = base("add_eca", "medium", op)
    local ok, err = validate_trigger_index(cleaned, op)
    if not ok then return nil, err end
    ok, err = validate_eca(cleaned, op)
    if not ok then return nil, err end
    local func = field(op, { "func", "func_name", "funcName" })
    if func ~= nil then
        if not is_string(func, 256) then
            return nil, "func must be a non-empty string up to 256 bytes"
        end
        cleaned.func = func
    end
    return cleaned
end

validators.remove_eca = function(op)
    local cleaned = base("remove_eca", "high", op)
    local ok, err = validate_trigger_index(cleaned, op)
    if not ok then return nil, err end
    ok, err = validate_eca(cleaned, op)
    if not ok then return nil, err end
    ok, err = validate_eca_index(cleaned, op)
    if not ok then return nil, err end
    return cleaned
end

validators.object_set_field = function(op)
    local cleaned = base("object_set_field", "review", op)
    local type_name = field(op, { "type_name", "type", "object_type", "objectType" })
    if not OBJECT_TYPES[type_name] then
        return nil, "type_name must be a known object editor type"
    end
    local record_kind = field(op, { "record_kind", "recordKind", "section" }) or "custom"
    if record_kind ~= "original" and record_kind ~= "custom" then
        return nil, "record_kind must be original or custom"
    end
    local object_id = field(op, { "object_id", "objectId", "id" })
    local field_id = field(op, { "field_id", "fieldId", "field" })
    if not is_fourcc(object_id) then
        return nil, "object_id must be a 4-byte object id"
    end
    if not is_fourcc(field_id) then
        return nil, "field_id must be a 4-byte field id"
    end
    local value = field(op, { "value" })
    if not primitive(value) then
        return nil, "value must be string, number, or boolean"
    end
    cleaned.type_name = type_name
    cleaned.record_kind = record_kind
    cleaned.object_id = object_id
    cleaned.field_id = field_id
    cleaned.value = value
    return cleaned
end

local function source_operations(plan)
    if type(plan) ~= "table" then
        return nil
    end
    local ops = plan.operations or plan.actions or plan.edits or plan
    if type(ops) ~= "table" then
        return nil
    end
    return ops
end

function M.validate_plan(plan)
    local result = {
        ok = true,
        summary = type(plan) == "table" and (plan.summary or plan.explanation) or nil,
        operations = {},
        warnings = {},
        errors = {},
    }

    local operations = source_operations(plan)
    if not operations then
        result.ok = false
        add_error(result.errors, 0, "plan must be an object with operations array")
        return result
    end

    if #operations == 0 then
        result.ok = false
        add_error(result.errors, 0, "operations must be a non-empty array")
        return result
    end

    for i, op in ipairs(operations) do
        if type(op) ~= "table" then
            result.ok = false
            add_error(result.errors, i, "operation must be an object")
        else
            local action = op.op or op.action or op.type
            local validator = validators[action]
            if not validator then
                result.ok = false
                add_error(result.errors, i, "unsupported operation: " .. tostring(action))
            else
                local cleaned, err = validator(op)
                if cleaned then
                    result.operations[#result.operations + 1] = cleaned
                    if cleaned.risk == "high" or cleaned.risk == "review" then
                        add_warning(result.warnings, i, "operation requires explicit user review")
                    end
                else
                    result.ok = false
                    add_error(result.errors, i, err)
                end
            end
        end
    end

    return result
end

function M.schema()
    return {
        response_format = {
            summary = "short explanation",
            operations = "array of operations",
        },
        operations = {
            "set_trigger_name",
            "set_trigger_disabled",
            "set_eca_func_name",
            "set_eca_active",
            "set_eca_param_value",
            "add_eca",
            "remove_eca",
            "object_set_field",
        },
        eca_types = ECA_TYPES,
        object_types = OBJECT_TYPES,
        safety = "Return operations only. Do not apply changes directly. High-risk operations require user review.",
    }
end

return M
