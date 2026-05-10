local M = {}

local ECA_TYPES = {
    [0] = "event",
    [1] = "condition",
    [2] = "action",
}

local OBJECT_TYPES = {
    unit = true,
    item = true,
    destructable = true,
    destructible = true,
    doodad = true,
    ability = true,
    buff = true,
    upgrade = true,
}

local SEMANTIC_TEMPLATES = {
    {
        name = "quest.create",
        status = "rpc",
        category = "quest",
        purpose = "Create a GUI quest object and store its handle in a global variable.",
        required_globals = { "quest" },
        emits = { "CreateQuestBJ", "QuestSetTitleBJ", "QuestSetDescriptionBJ", "QuestSetIconPathBJ" },
        verification = "Read the generated GUI trigger ECA list, save the map, and verify the quest global exists.",
    },
    {
        name = "quest.complete_when",
        status = "rpc",
        category = "quest",
        purpose = "Evaluate a GUI condition and mark a quest or quest item complete.",
        required_globals = { "quest", "questitem" },
        emits = { "QuestSetCompletedBJ", "QuestItemSetCompletedBJ", "DisplayTimedTextToForce" },
        verification = "Loopback the condition/action ECA list and run save_map.",
    },
    {
        name = "creep_spawn.periodic",
        status = "rpc",
        category = "spawn",
        purpose = "Create periodic neutral hostile creep spawns in a region with count caps.",
        required_globals = { "rect", "group", "timer", "integer" },
        emits = { "TriggerRegisterTimerEventPeriodic", "CountUnitsInGroup", "CreateNUnitsAtLoc" },
        verification = "Verify timer event, cap condition, spawn action, and cleanup actions are present.",
    },
    {
        name = "leaderboard.create_or_update",
        status = "rpc",
        category = "ui",
        purpose = "Create a GUI leaderboard and update player rows from integer globals.",
        required_globals = { "leaderboard", "integer" },
        emits = { "CreateLeaderboardBJ", "LeaderboardAddItemBJ", "LeaderboardSetItemValueBJ", "LeaderboardDisplayBJ" },
        verification = "Verify leaderboard global, create action, row update actions, and save_map.",
    },
    {
        name = "timer_window.countdown",
        status = "rpc",
        category = "ui",
        purpose = "Create a timer, attach a timer dialog, start it, and run timeout actions.",
        required_globals = { "timer", "timerdialog" },
        emits = { "CreateTimerDialogBJ", "StartTimerBJ", "TriggerRegisterTimerExpireEventBJ", "DestroyTimerDialogBJ" },
        verification = "Verify timer/dialog globals, start action, expire event, and cleanup action.",
    },
    {
        name = "dialog.choice",
        status = "rpc",
        category = "ui",
        purpose = "Create a GUI dialog with buttons and route clicked-button responses.",
        required_globals = { "dialog", "button" },
        emits = { "DialogSetMessageBJ", "DialogAddButtonBJ", "DialogDisplayBJ", "TriggerRegisterDialogButtonEventBJ" },
        verification = "Verify dialog/button globals, button events, branch conditions, and save_map.",
    },
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
    if op.params ~= nil then
        if type(op.params) ~= "table" then
            return nil, "params must be an array when provided"
        end
        cleaned.params = {}
        for i, value in ipairs(op.params) do
            if not primitive(value) then
                return nil, "params entries must be string, number, or boolean"
            end
            cleaned.params[i] = tostring(value)
        end
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
        semantic_templates = SEMANTIC_TEMPLATES,
        usage_contract = {
            "Return operations only; never mutate map files outside ai.apply_plan or documented CLI helpers.",
            "Prefer semantic templates from docs/agent-gui-api.md for gameplay systems instead of hand-picking random GUI function names.",
            "Use ai.template_plan or ai.apply_template for semantic templates, then verify every emitted ECA after save_map.",
            "Do not call remove_eca unless allow_non_recoverable=true is intentionally set and the caller accepts that rollback cannot restore the removed GUI node.",
        },
        safety = "Return operations only. Do not apply changes directly. High-risk operations require user review. remove_eca is non-rollback-safe and requires allow_non_recoverable=true when applying.",
    }
end

local function require_template_arg(args, name)
    local value = args and args[name]
    if value == nil or value == "" then
        return nil, name .. " is required"
    end
    return value
end

local function template_trigger_index(args)
    local idx = args and (args.trigger_index or args.triggerIndex)
    if not is_integer(idx, 0) then
        return nil, "trigger_index must be a non-negative integer"
    end
    return idx
end

local function action(trigger_index, func, params)
    return {
        op = "add_eca",
        trigger_index = trigger_index,
        eca_type = 2,
        func = func,
        params = params or {},
    }
end

local function event(trigger_index, func, params)
    return {
        op = "add_eca",
        trigger_index = trigger_index,
        eca_type = 0,
        func = func,
        params = params or {},
    }
end

local function condition(trigger_index, func, params)
    return {
        op = "add_eca",
        trigger_index = trigger_index,
        eca_type = 1,
        func = func,
        params = params or {},
    }
end

local template_builders = {}

template_builders["quest.create"] = function(args)
    local trigger_index, err = template_trigger_index(args)
    if not trigger_index then return nil, err end
    local quest_global; quest_global, err = require_template_arg(args, "quest_global")
    if not quest_global then return nil, err end
    local title; title, err = require_template_arg(args, "title")
    if not title then return nil, err end
    local description = args.description or ""
    local icon_path = args.icon_path or args.iconPath or ""
    local required = args.required ~= false
    return {
        summary = "Create GUI quest",
        operations = {
            action(trigger_index, "CreateQuestBJ", { tostring(required), title, description, icon_path }),
            action(trigger_index, "SetVariable", { quest_global, "GetLastCreatedQuestBJ" }),
        },
    }
end

template_builders["quest.complete_when"] = function(args)
    local trigger_index, err = template_trigger_index(args)
    if not trigger_index then return nil, err end
    local quest_global; quest_global, err = require_template_arg(args, "quest_global")
    if not quest_global then return nil, err end
    local condition_func = args.condition_func or args.conditionFunc
    local condition_params = args.condition_params or args.conditionParams or {}
    local operations = {}
    if condition_func then
        operations[#operations + 1] = condition(trigger_index, condition_func, condition_params)
    end
    operations[#operations + 1] = action(trigger_index, "QuestSetCompletedBJ", { quest_global, "true" })
    if args.message and args.message ~= "" then
        operations[#operations + 1] = action(trigger_index, "DisplayTimedTextToForce", { "GetPlayersAll()", tostring(args.message), tostring(args.duration or 10) })
    end
    return {
        summary = "Complete GUI quest from condition",
        operations = operations,
    }
end

template_builders["creep_spawn.periodic"] = function(args)
    local trigger_index, err = template_trigger_index(args)
    if not trigger_index then return nil, err end
    local region_global; region_global, err = require_template_arg(args, "region_global")
    if not region_global then return nil, err end
    local unit_id; unit_id, err = require_template_arg(args, "unit_id")
    if not unit_id then return nil, err end
    return {
        summary = "Create periodic creep spawn GUI trigger",
        operations = {
            event(trigger_index, "TriggerRegisterTimerEventPeriodic", { tostring(args.interval_seconds or 30) }),
            condition(trigger_index, "CountLivingPlayerUnitsOfTypeId", { unit_id, tostring(args.owner_player or "Player(PLAYER_NEUTRAL_AGGRESSIVE)"), "<", tostring(args.max_alive or 12) }),
            action(trigger_index, "CreateNUnitsAtLoc", { tostring(args.spawn_count or 1), unit_id, tostring(args.owner_player or "Player(PLAYER_NEUTRAL_AGGRESSIVE)"), region_global }),
        },
    }
end

template_builders["leaderboard.create_or_update"] = function(args)
    local trigger_index, err = template_trigger_index(args)
    if not trigger_index then return nil, err end
    local leaderboard_global; leaderboard_global, err = require_template_arg(args, "leaderboard_global")
    if not leaderboard_global then return nil, err end
    local title = args.title or "Score"
    local operations = {
        action(trigger_index, "CreateLeaderboardBJ", { "GetPlayersAll()", title }),
        action(trigger_index, "SetVariable", { leaderboard_global, "GetLastCreatedLeaderboard()" }),
    }
    for _, row in ipairs(args.rows or {}) do
        operations[#operations + 1] = action(trigger_index, "LeaderboardAddItemBJ", {
            leaderboard_global,
            tostring(row.label or row.player or "Player"),
            tostring(row.value_global or row.valueGlobal or "0"),
            tostring(row.player or "Player(0)"),
        })
    end
    operations[#operations + 1] = action(trigger_index, "LeaderboardDisplayBJ", { tostring(args.display ~= false), leaderboard_global })
    return {
        summary = "Create or update GUI leaderboard",
        operations = operations,
    }
end

template_builders["timer_window.countdown"] = function(args)
    local trigger_index, err = template_trigger_index(args)
    if not trigger_index then return nil, err end
    local timer_global; timer_global, err = require_template_arg(args, "timer_global")
    if not timer_global then return nil, err end
    local timer_dialog_global; timer_dialog_global, err = require_template_arg(args, "timer_dialog_global")
    if not timer_dialog_global then return nil, err end
    return {
        summary = "Create countdown timer window",
        operations = {
            action(trigger_index, "CreateTimerBJ", { tostring(args.duration_seconds or 60), "false" }),
            action(trigger_index, "SetVariable", { timer_global, "GetLastCreatedTimerBJ()" }),
            action(trigger_index, "CreateTimerDialogBJ", { timer_global, tostring(args.title or "Countdown") }),
            action(trigger_index, "SetVariable", { timer_dialog_global, "GetLastCreatedTimerDialogBJ()" }),
            event(trigger_index, "TriggerRegisterTimerExpireEventBJ", { timer_global }),
        },
    }
end

template_builders["dialog.choice"] = function(args)
    local trigger_index, err = template_trigger_index(args)
    if not trigger_index then return nil, err end
    local dialog_global; dialog_global, err = require_template_arg(args, "dialog_global")
    if not dialog_global then return nil, err end
    local operations = {
        action(trigger_index, "DialogSetMessageBJ", { dialog_global, tostring(args.message or "") }),
    }
    for _, button in ipairs(args.buttons or {}) do
        local text = tostring(button.text or button.label or "Option")
        local global = button.global or button.button_global or button.buttonGlobal
        operations[#operations + 1] = action(trigger_index, "DialogAddButtonBJ", { dialog_global, text })
        if global then
            operations[#operations + 1] = action(trigger_index, "SetVariable", { global, "GetLastCreatedButtonBJ()" })
            operations[#operations + 1] = event(trigger_index, "TriggerRegisterDialogButtonEventBJ", { global })
        end
    end
    operations[#operations + 1] = action(trigger_index, "DialogDisplayBJ", { tostring(args.player or "Player(0)"), dialog_global, "true" })
    return {
        summary = "Create GUI dialog choice flow",
        operations = operations,
    }
end

function M.template_plan(template_name, args)
    local builder = template_builders[template_name]
    if not builder then
        return nil, "unsupported template: " .. tostring(template_name)
    end
    local plan, err = builder(args or {})
    if not plan then
        return nil, err
    end
    local validation = M.validate_plan(plan)
    return {
        ok = validation.ok,
        template = template_name,
        plan = plan,
        validation = validation,
    }
end

return M
