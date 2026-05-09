local compiler = require "compiler"
local map_packer = require 'w3x2lni.map_packer'
local dev = fs.ydwe_devpath()

local function normalize_map_key(path)
    if type(path) ~= "string" then
        path = tostring(path)
    end
    path = path:gsub("/", "\\")
    path = path:gsub("^%s+", ""):gsub("%s+$", "")
    if path == "" then
        return nil
    end
    return path:lower()
end

local function pending_globals_path()
    return fs.ydwe_path() / 'logs' / 'ydagent_pending_globals.lua'
end

local function sorted_keys(tbl)
    local keys = {}
    for key in pairs(tbl or {}) do
        keys[#keys + 1] = key
    end
    table.sort(keys)
    return keys
end

local function load_pending_global_overrides()
    local path = pending_globals_path()
    local chunk = loadfile(path:string())
    if not chunk then
        return {}
    end
    local ok, data = pcall(chunk)
    if not ok or type(data) ~= "table" then
        return {}
    end
    return data
end

local function save_pending_global_overrides(data)
    local path = pending_globals_path()
    if next(data or {}) == nil then
        fs.remove(path)
        return true
    end
    local f = io.open(path, 'wb')
    if not f then
        return nil, 'cannot write pending globals'
    end
    f:write("return {\n")
    for _, map_key in ipairs(sorted_keys(data)) do
        local entry = data[map_key]
        if type(entry) == "table" and next(entry) ~= nil then
            f:write("  [", string.format("%q", map_key), "] = {\n")
            for _, global_name in ipairs(sorted_keys(entry)) do
                local item = entry[global_name]
                f:write(
                    "    [", string.format("%q", global_name), "] = { type_name = ",
                    string.format("%q", tostring(item.type_name or "")),
                    ", value = ", string.format("%q", tostring(item.value or "")), " },\n"
                )
            end
            f:write("  },\n")
        end
    end
    f:write("}\n")
    f:close()
    return true
end

local function parse_variable_lml(path)
    local f = io.open(path, 'rb')
    if not f then
        return nil, 'cannot open variable.lml'
    end
    local vars = {}
    local current = nil
    for raw_line in f:lines() do
        if raw_line:match("^%s*$") then
        elseif not raw_line:match("^    ") then
            local name, type_name = raw_line:match("^([^:]+):%s*(.-)%s*$")
            if not name or not type_name then
                f:close()
                return nil, 'invalid variable declaration'
            end
            current = {
                name = name:gsub("^%s+", ""):gsub("%s+$", ""),
                type_name = type_name:gsub("^%s+", ""):gsub("%s+$", ""),
                options = {},
                option_order = {},
            }
            vars[#vars + 1] = current
        else
            if not current then
                f:close()
                return nil, 'orphan variable option'
            end
            local key, value = raw_line:match("^%s+([^:]+):%s*(.-)%s*$")
            if key then
                key = key:gsub("^%s+", ""):gsub("%s+$", "")
                current.options[key] = value
                current.option_order[#current.option_order + 1] = key
            end
        end
    end
    f:close()
    return vars
end

local function write_variable_lml(path, vars)
    local f = io.open(path, 'wb')
    if not f then
        return nil, 'cannot write variable.lml'
    end
    for _, var in ipairs(vars or {}) do
        f:write(var.name, ": ", var.type_name, "\n")
        local emitted = {}
        for _, key in ipairs(var.option_order or {}) do
            if var.options[key] ~= nil then
                f:write("    ", key, ": ", tostring(var.options[key]), "\n")
                emitted[key] = true
            end
        end
        for key, value in pairs(var.options or {}) do
            if not emitted[key] then
                f:write("    ", key, ": ", tostring(value), "\n")
            end
        end
    end
    f:close()
    return true
end

local function normalize_global_name(name)
    if type(name) ~= 'string' then
        return nil
    end
    name = name:gsub("^%s+", ""):gsub("%s+$", "")
    if name:match("^udg_[%a_][%w_]*$") then
        return name:sub(5)
    end
    return name
end

local function find_variable(vars, name)
    local normalized = normalize_global_name(name)
    if not normalized then
        return nil
    end
    for _, var in ipairs(vars or {}) do
        if var.name == normalized then
            return var
        end
    end
    return nil
end

local function ensure_default_option(var)
    for _, key in ipairs(var.option_order or {}) do
        if key:match("^Def") then
            return key
        end
    end
    local key = "Def   "
    var.option_order = var.option_order or {}
    var.option_order[#var.option_order + 1] = key
    return key
end

local function clear_pending_global_overrides(source_path)
    local data = load_pending_global_overrides()
    local map_key = normalize_map_key(source_path:string())
    if not map_key or data[map_key] == nil then
        return true
    end
    data[map_key] = nil
    return save_pending_global_overrides(data)
end

local function escape_lua_pattern(text)
    return (tostring(text):gsub("([%%%^%$%(%)%%.%[%]%*%+%-%?])", "%%%1"))
end

local function format_jass_value(type_name, value)
    value = tostring(value or "")
    if type_name == "string" then
        value = value:gsub("\\", "\\\\"):gsub('"', '\\"')
        return '"' .. value .. '"'
    end
    if type_name == "boolean" then
        local lower = value:lower()
        if lower == "1" then
            return "true"
        end
        if lower == "0" then
            return "false"
        end
        if lower == "true" or lower == "false" then
            return lower
        end
    end
    return value
end

local function encode_wtg_value(type_name, value)
    value = tostring(value or "")
    if type_name == "boolean" then
        local lower = value:lower()
        if lower == "true" then
            return "1"
        end
        if lower == "false" then
            return "0"
        end
    end
    return value
end

local function patch_wtg_global_default(text, global_name, type_name, value)
    local name = normalize_global_name(global_name)
    if not name or name == "" then
        return text, false
    end

    local needle = name .. "\0" .. tostring(type_name or "") .. "\0"
    local pos = text:find(needle, 1, true)
    if not pos then
        return text, false
    end

    local value_start = pos + #needle + 16
    if value_start > #text + 1 then
        return text, false
    end

    local value_end = text:find("\0", value_start, true)
    if not value_end then
        return text, false
    end

    local encoded = encode_wtg_value(type_name, value)
    return text:sub(1, value_start - 1) .. encoded .. text:sub(value_end), true
end

local function apply_pending_globals_to_wtg(wtg_path, entry)
    local text = io.load(wtg_path)
    if not text then
        return nil, 'cannot open war3map.wtg'
    end

    local changed = false
    for global_name, item in pairs(entry or {}) do
        local next_text, ok = patch_wtg_global_default(
            text,
            global_name,
            tostring(item.type_name or ""),
            tostring(item.value or "")
        )
        if ok then
            text = next_text
            changed = true
        else
            log.warn('Pending global not found in war3map.wtg: ' .. tostring(global_name))
        end
    end

    if not changed then
        return false
    end
    return io.save(wtg_path, text)
end

local function apply_pending_globals_to_war3map(script_path, entry)
    local text = io.load(script_path)
    if not text then
        return nil, 'cannot open war3map.j'
    end
    local lines = {}
    for line in (text .. "\n"):gmatch("(.-)\r?\n") do
        lines[#lines + 1] = line
    end
    if lines[#lines] == "" then
        lines[#lines] = nil
    end

    local formatted = {}
    local replaced_set = {}
    for global_name, item in pairs(entry or {}) do
        formatted[global_name] = format_jass_value(item.type_name, item.value)
        replaced_set[global_name] = false
    end

    local in_globals = false
    local in_init_globals = false
    local init_end_index = nil
    for i, line in ipairs(lines) do
        if line:match("^%s*globals%s*$") then
            in_globals = true
        elseif line:match("^%s*endglobals%s*$") then
            in_globals = false
        elseif line:match("^%s*function%s+InitGlobals%s+takes%s+nothing%s+returns%s+nothing%s*$") then
            in_init_globals = true
        elseif in_init_globals and line:match("^%s*endfunction%s*$") then
            init_end_index = i
            in_init_globals = false
        elseif in_globals or in_init_globals then
            for global_name, replacement in pairs(formatted) do
                local escaped_name = escape_lua_pattern(global_name)
                if in_globals then
                    local new_line, count = line:gsub(
                        "^(%s*constant%s+[%w_]+%s+" .. escaped_name .. "%s*=%s*).-$",
                        "%1" .. replacement
                    )
                    if count == 0 then
                        new_line, count = line:gsub(
                            "^(%s*[%w_]+%s+" .. escaped_name .. "%s*=%s*).-$",
                            "%1" .. replacement
                        )
                    end
                    if count > 0 then
                        line = new_line
                    end
                end
                if in_init_globals then
                    local new_line, count = line:gsub(
                        "^(%s*set%s+" .. escaped_name .. "%s*=%s*).-$",
                        "%1" .. replacement
                    )
                    if count > 0 then
                        line = new_line
                        replaced_set[global_name] = true
                    end
                end
            end
            lines[i] = line
        end
    end

    if init_end_index then
        local inserts = {}
        for global_name, replacement in pairs(formatted) do
            if not replaced_set[global_name] then
                inserts[#inserts + 1] = "    set " .. global_name .. " = " .. replacement
            end
        end
        if #inserts > 0 then
            table.sort(inserts)
            for index = #inserts, 1, -1 do
                table.insert(lines, init_end_index, inserts[index])
            end
        end
    end

    return io.save(script_path, table.concat(lines, "\n") .. "\n")
end

local function apply_pending_global_overrides(source_path, temp_path)
    local data = load_pending_global_overrides()
    local map_key = normalize_map_key(source_path:string())
    local entry = map_key and data[map_key] or nil
    if type(entry) ~= "table" or next(entry) == nil then
        return false
    end
    local variable_path = temp_path / 'trigger' / 'variable.lml'
    if fs.exists(variable_path) then
        local vars, err = parse_variable_lml(variable_path)
        if not vars then
            return nil, err
        end
        for global_name, item in pairs(entry) do
            local var = find_variable(vars, global_name)
            if var then
                if item.type_name and item.type_name ~= "" then
                    var.type_name = item.type_name
                end
                local default_key = ensure_default_option(var)
                var.options[default_key] = tostring(item.value or "")
            else
                log.warn('Pending global not found in variable.lml: ' .. tostring(global_name))
            end
        end
        local ok, write_err = write_variable_lml(variable_path, vars)
        if not ok then
            return nil, write_err
        end
        log.info('Applied pending globals through variable.lml for ' .. source_path:string())
        return map_key, true
    end
    local wtg_path = temp_path / 'war3map.wtg'
    if fs.exists(wtg_path) then
        local ok, err = apply_pending_globals_to_wtg(wtg_path, entry)
        if err ~= nil then
            return nil, err
        end
        if ok then
            local war3map_path = temp_path / 'war3map.j'
            if fs.exists(war3map_path) then
                apply_pending_globals_to_war3map(war3map_path, entry)
            end
            log.info('Applied pending globals through war3map.wtg for ' .. source_path:string())
            return map_key, false
        end
    end

    local war3map_path = temp_path / 'war3map.j'
    if fs.exists(war3map_path) then
        local ok, err = apply_pending_globals_to_war3map(war3map_path, entry)
        if not ok then
            return nil, err
        end
        log.info('Applied pending globals through war3map.j for ' .. source_path:string())
        return map_key, false
    end
    return nil, 'cannot open variable.lml or war3map.j'
end

local function backup_map(map_path)
    local ydwe_path = fs.ydwe_path()
    fs.create_directories(ydwe_path / 'backups')
    local buf = io.load(ydwe_path / 'backups' / 'backupsdata.txt')
    local char
    if buf then
        char = buf:match '(.)[\r\n]*$'
    else
        char = '0'
    end
    local filename = char .. map_path:extension():string()
    local target_path = ydwe_path / 'backups' / filename
    log.info('Backup map at ' .. target_path:string())
    fs.copy_file(map_path, target_path, true)
end

local function saveW3x(source_path, target_path, temp_path, save_version, is_test)
    fs.remove(target_path)
    local applied_key, clear_after_save, apply_err = apply_pending_global_overrides(source_path, temp_path)
    if apply_err ~= nil then
        log.error('Apply pending globals failed: ' .. tostring(apply_err))
        return false
    end
    local result = compiler:compile(temp_path, global_config, save_version)
    log.debug("Compiler Result " .. tostring(result))
    
    local result
    if is_test then
        local mapSlk = "0" ~= global_config["MapTest"]["EnableMapSlk"]
        if mapSlk then
            result = map_packer('slk', temp_path, target_path)
        else
            result = map_packer('pack', temp_path, target_path)
        end
        backup_map(target_path)
    else
        if target_path:filename():string() == '.w3x' then
            result = map_packer('lni', temp_path, source_path:parent_path())
            fs.copy_file(dev / 'plugin' / 'w3x2lni' / 'script' / 'core' / '.w3x', target_path, true)
        else
            result = map_packer('pack', temp_path, target_path)
            backup_map(target_path)
        end
    end
    log.debug("Packer Result " .. tostring(result))
    if result and applied_key and clear_after_save then
        clear_pending_global_overrides(source_path)
    end
    return result
end

local function saveW3m(source_path, target_path, temp_path, save_version)
    fs.remove(target_path)
    local result = compiler:compile(temp_path, global_config, save_version)
    log.debug("Compiler Result " .. tostring(result))
    
    local result
    result = map_packer('pack', temp_path, target_path)
    backup_map(target_path)
    log.debug("Packer Result " .. tostring(result))
    return result
end

local function saveW3n(source_path, target_path, temp_path, save_version)
    fs.remove(target_path)
    
    local result
    result = map_packer('pack', temp_path, target_path)
    backup_map(target_path)
    log.debug("Packer Result " .. tostring(result))
    return result
end

function event.EVENT_NEW_SAVE_MAP(event_data)
	log.debug("********************* on save start *********************")

	-- 刷新配置数据
	global_config_reload()

    local target_path = fs.path(event_data.map_path)
    local temp_path = target_path:parent_path()
    local source_path = temp_path:parent_path() / target_path:filename()
    
    if event_data.test then
        log.debug("Test Map")
    else
        log.debug("Save Map")
    end
    log.info("Saving " .. source_path:string())
    local save_type = temp_path:filename():string():sub(-7, -5)
    local save_version = war3_version:is_new() and 24 or 20
    log.info("Type:", save_type, "Version:", save_version)

	-- 如果地图文件带有只读属性，则先询问是否去掉只读属性
	-- 128 == 0200 S_IWUSR
	if fs.exists(source_path) and 0 == (source_path:permissions() & 128) then
		if gui.yesno_message(nil, LNG.REMOVE_MAP_READONLY, source_path:string()) then
			log.trace("Remove the read-only attribute.")
			source_path:add_permissions(128)
		else
            log.trace("Don't remove the read-only attribute.")
            log.debug("********************* on save end *********************")
            return -1
        end
    end

    local result = false
    if save_type == 'w3x' then
        result = saveW3x(source_path, target_path, temp_path, save_version, event_data.test)
    elseif save_type == 'w3m' then
        result = saveW3m(source_path, target_path, temp_path, save_version)
    elseif save_type == 'w3n' then
        result = saveW3n(source_path, target_path, temp_path, save_version)
    else
        log.error('Unsupport save to ' .. save_type)
        gui.error_message(nil, LNG.UNSUPORTED_SAVE_TYPE, save_type)
    end

	log.debug("********************* on save end *********************")
	if result then return 0 else return -1 end
end
