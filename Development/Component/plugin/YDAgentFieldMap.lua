local M = {}

local loaded_key = nil
local fields = {}
local sources = {}

local META_FILES = {
    unit = [[share\zh-CN\mpq\units\unitmetadata.slk]],
    ability = [[share\zh-CN\mpq\units\abilitymetadata.slk]],
    buff = [[share\zh-CN\mpq\units\abilitybuffmetadata.slk]],
    doodad = [[share\zh-CN\mpq\doodads\doodadmetadata.slk]],
    destructable = [[share\zh-CN\mpq\units\destructablemetadata.slk]],
    upgrade = [[share\zh-CN\mpq\units\upgrademetadata.slk]],
    upgrade_effect = [[share\zh-CN\mpq\units\upgradeeffectmetadata.slk]],
    misc = [[share\zh-CN\mpq\units\miscmetadata.slk]],
}

local WES_FILES = {
    [[share\zh-CN\mpq\ui\WorldEditStrings.txt]],
    [[share\zh-CN\mpq\units\CommandStrings.txt]],
}

local function join(root, rel)
    if root:sub(-1) == "\\" or root:sub(-1) == "/" then
        return root .. rel
    end
    return root .. "\\" .. rel
end

local function read_file(path)
    local f = io.open(path, "rb")
    if not f then
        return nil
    end
    local data = f:read("*a")
    f:close()
    return data
end

local function trim(s)
    return (s or ""):gsub("^%s+", ""):gsub("%s+$", "")
end

local function parse_k(raw)
    if not raw then
        return ""
    end
    raw = trim(raw)
    if raw:sub(1, 1) == '"' and raw:sub(-1) == '"' then
        raw = raw:sub(2, -2)
        raw = raw:gsub('""', '"')
    end
    return raw
end

local function load_wes(root)
    local result = {}
    for _, rel in ipairs(WES_FILES) do
        local data = read_file(join(root, rel))
        if data then
            for line in data:gmatch("[^\r\n]+") do
                local key, value = line:match("^%s*([%w_]+)%s*=%s*(.-)%s*$")
                if key and value then
                    result[key] = parse_k(value)
                end
            end
        end
    end
    return result
end

local function parse_slk(data)
    local headers = {}
    local rows = {}
    local current_y = nil

    for line in data:gmatch("[^\r\n]+") do
        if line:sub(1, 2) == "C;" or line == "C" then
            local x = tonumber(line:match(";X(%d+)"))
            local y = tonumber(line:match(";Y(%d+)"))
            local raw = line:match(";K(.*)$")
            if y then
                current_y = y
            end
            if x and current_y and raw ~= nil then
                local value = parse_k(raw)
                if current_y == 1 then
                    headers[x] = value
                else
                    rows[current_y] = rows[current_y] or {}
                    local key = headers[x] or tostring(x)
                    rows[current_y][key] = value
                end
            end
        end
    end

    return rows
end

local function resolve_source(default_source, row)
    if default_source == "unit" and row.slk == "ItemData" then
        return "item"
    end
    if default_source == "upgrade_effect" then
        return "upgrade"
    end
    return default_source
end

local function put_field(id, row, source, wes)
    if not id or id == "" then
        return
    end

    local display_key = row.displayName
    local display_name = display_key and wes[display_key] or nil
    local entry = {
        id = id,
        field = row.field,
        displayNameKey = display_key,
        name = display_name or display_key or row.field or id,
        category = row.category,
        type = row.type,
        slk = row.slk,
        index = tonumber(row.index),
        section = row.section,
        source = resolve_source(source, row),
    }

    fields[id] = entry
    sources[entry.source] = sources[entry.source] or {}
    sources[entry.source][id] = entry
end

function M.load(component_root)
    component_root = component_root or ""
    if loaded_key == component_root then
        return true
    end

    local next_fields = {}
    local next_sources = {}
    fields = next_fields
    sources = next_sources

    local wes = load_wes(component_root)
    for source, rel in pairs(META_FILES) do
        local data = read_file(join(component_root, rel))
        if data then
            local rows = parse_slk(data)
            for _, row in pairs(rows) do
                put_field(row.ID, row, source, wes)
            end
        end
    end

    loaded_key = component_root
    return true
end

function M.get(field_id, source)
    if source and sources[source] then
        return sources[source][field_id] or fields[field_id]
    end
    return fields[field_id]
end

function M.name(field_id, source)
    local entry = M.get(field_id, source)
    return entry and entry.name or field_id
end

function M.all(source)
    if source then
        return sources[source] or {}
    end
    return fields
end

function M.summary(source)
    local src = M.all(source)
    local list = {}
    for id, item in pairs(src) do
        list[#list + 1] = {
            id = id,
            name = item.name,
            field = item.field,
            category = item.category,
            type = item.type,
            source = item.source,
        }
    end
    table.sort(list, function(a, b)
        return a.id < b.id
    end)
    return list
end

function M.annotate_fields(fields_obj, source)
    local result = {}
    for id, value in pairs(fields_obj or {}) do
        local info = M.get(id, source)
        result[id] = {
            value = value,
            name = info and info.name or id,
            field = info and info.field or nil,
            category = info and info.category or nil,
            type = info and info.type or nil,
            source = info and info.source or nil,
        }
    end
    return result
end

return M
