local ffi = require "ffi"
local log = require "log"
require "bee"
local socket = require "bee.socket"
local thread = require "bee.thread"
local ok_sleep, sleep = pcall(require, "ffi.sleep")
if not ok_sleep then
    ffi.cdef[[void __stdcall Sleep(unsigned long dwMilliseconds);]]
    sleep = function(ms)
        ffi.C.Sleep(ms or 0)
    end
end
local ok_uni, uni = pcall(require, "ffi.unicode")
if not ok_uni then
    uni = {}
    function uni.u2w(text)
        text = tostring(text or "")
        local buf = ffi.new("wchar_t[?]", #text + 1)
        for i = 1, #text do
            buf[i - 1] = text:byte(i)
        end
        buf[#text] = 0
        return buf
    end
    function uni.w2u(buf)
        local out = {}
        for i = 0, 255 do
            local ch = tonumber(buf[i])
            if not ch or ch == 0 then
                break
            end
            out[#out + 1] = string.char(ch % 256)
        end
        return table.concat(out)
    end
end

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

local function open_channel(name)
    local ok, ch = pcall(thread.channel, name)
    if ok then
        return ch
    end
    if thread.newchannel then
        pcall(thread.newchannel, name)
    end
    ok, ch = pcall(thread.channel, name)
    return ok and ch or nil
end

ffi.cdef[[
    typedef int HWND;
    typedef int HMENU;
    typedef int WPARAM;
    typedef int LPARAM;
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
    int  ydt_create_trigger(const char* name);
    int  ydt_delete_trigger(int trig_index);
    int  ydt_get_global_count(void);
    const char* ydt_get_global_name(int index);
    int  ydt_get_global_type(int index);
    const char* ydt_get_global_value(int index);
    const char* ydt_global_diag(void);
    const char* ydt_read_object_file(const char* file_path);
    int  ydt_write_object_file(const char* file_path, const char* json_data);
    const char* ydt_mem_dump(uint32_t addr, uint32_t size);
    int __stdcall GetCurrentProcessId();
    int __stdcall FindWindowExW(int hWndParent, int hWndChildAfter, const wchar_t* lpszClass, const wchar_t* lpszWindow);
    int __stdcall GetWindowThreadProcessId(int hWnd, int* lpdwProcessId);
    int __stdcall SendMessageW(HWND hWnd, unsigned int Msg, WPARAM wParam, LPARAM lParam);
    int __stdcall SetForegroundWindow(HWND hWnd);
    int __stdcall BringWindowToTop(HWND hWnd);
    HMENU __stdcall GetMenu(HWND hWnd);
    HMENU __stdcall GetSubMenu(HMENU hMenu, int nPos);
    int __stdcall GetMenuItemCount(HMENU hMenu);
    int __stdcall GetMenuStringW(HMENU hMenu, unsigned int uIDItem, wchar_t* lpString, int cchMax, unsigned int uFlag);
    unsigned int __stdcall GetMenuItemID(HMENU hMenu, int nPos);
]]

local YDT = rawget(_G, "YDAGENT_TEST_STUB")
if not YDT then
    local ok_ydt, loaded_ydt = pcall(ffi.load, DLL_PATH)
    if not ok_ydt then
        log.error("YDAgentServer: failed to load YDTrigger.dll: " .. tostring(loaded_ydt))
        return
    end
    YDT = loaded_ydt
end

local WM_KEYDOWN = 0x0100
local WM_KEYUP = 0x0101
local WM_COMMAND = 0x0111
local VK_F10 = 0x79
local MF_BYPOSITION = 0x0400
local WAR3_WINDOW_CLASS = uni.u2w("Warcraft III")

local function find_editor_window()
    local current_pid = ffi.C.GetCurrentProcessId()
    local pid = ffi.new("int[1]", 0)
    local hwnd = 0
    while true do
        hwnd = ffi.C.FindWindowExW(0, hwnd, WAR3_WINDOW_CLASS, nil)
        if hwnd == 0 then
            return nil
        end
        ffi.C.GetWindowThreadProcessId(hwnd, pid)
        if pid[0] == current_pid then
            return hwnd
        end
    end
end

local function send_key(hwnd, vk, delay_ms)
    ffi.C.SendMessageW(hwnd, WM_KEYDOWN, vk, 0)
    ffi.C.SendMessageW(hwnd, WM_KEYUP, vk, 0)
    if delay_ms and delay_ms > 0 then
        sleep(delay_ms)
    end
end

local function get_menu_text(menu, pos)
    local buf = ffi.new("wchar_t[256]")
    local len = ffi.C.GetMenuStringW(menu, pos, buf, 255, MF_BYPOSITION)
    if len <= 0 then
        return ""
    end
    return uni.w2u(buf, len)
end

local function is_save_caption(text)
    if not text or text == "" then
        return false
    end
    local lower = text:lower()
    if lower:find("save map as", 1, true) or lower:find("save as", 1, true) then
        return false
    end
    if lower:find("calculate shadows", 1, true) then
        return false
    end
    if text:find("另存为", 1, true) or text:find("阴影", 1, true) then
        return false
    end
    return lower:find("save map", 1, true) ~= nil or text:find("保存地图", 1, true) ~= nil
end

local function find_save_command_id(hwnd)
    local menu = ffi.C.GetMenu(hwnd)
    if not menu or menu == 0 then
        return nil
    end
    local top_count = ffi.C.GetMenuItemCount(menu)
    if top_count <= 0 then
        return nil
    end
    for top = 0, top_count - 1 do
        local submenu = ffi.C.GetSubMenu(menu, top)
        if submenu and submenu ~= 0 then
            local item_count = ffi.C.GetMenuItemCount(submenu)
            for pos = 0, item_count - 1 do
                local text = get_menu_text(submenu, pos)
                if is_save_caption(text) then
                    local command_id = ffi.C.GetMenuItemID(submenu, pos)
                    if command_id and command_id ~= 0xFFFFFFFF then
                        return tonumber(command_id), text
                    end
                end
            end
        end
    end
    return nil
end

local function current_map_path_for_save_guard()
    local root = COMPONENT_ROOT
    if root == "" then
        root = "."
    end
    local f = io.open(root .. "\\logs\\ydwe.log", "rb")
    if not f then
        return nil
    end
    local last = nil
    for line in f:lines() do
        local open_path = line:match("Open map\t(.+)$")
        if open_path and open_path ~= "" then
            last = open_path
        end
        local save_path = line:match("Saving%s+(.+)$")
        if save_path and save_path ~= "" then
            last = save_path
        end
    end
    f:close()
    if not last then
        return nil
    end
    last = last:gsub("[%z\001-\031]", "")
    last = last:gsub("/", "\\")
    return last:gsub("^%s+", ""):gsub("%s+$", "")
end

local function is_lni_marker_file_path(map_path)
    if type(map_path) ~= "string" or map_path == "" then
        return false
    end
    if not map_path:match("[/\\]%.[Ww]3[xmn]$") then
        return false
    end
    local f = io.open(map_path, "rb")
    if not f then
        return false
    end
    local buf = f:read(12) or ""
    f:close()
    return buf:sub(9, 12) == "W2L\001"
end

local editor = {}

function editor.save_map()
    local map_path = current_map_path_for_save_guard()
    if is_lni_marker_file_path(map_path) then
        return {
            ok = true,
            action = "lni_marker_noop",
            map_path = map_path,
            reason = "LNI marker maps are source-backed; GUI save rewrites the LNI source directory.",
        }
    end
    local hwnd = find_editor_window()
    if not hwnd or hwnd == 0 then
        return nil, "YDWE editor window not found"
    end
    ffi.C.SetForegroundWindow(hwnd)
    ffi.C.BringWindowToTop(hwnd)
    local command_id, caption = find_save_command_id(hwnd)
    if command_id then
        ffi.C.SendMessageW(hwnd, WM_COMMAND, command_id, 0)
        return {
            ok = true,
            hwnd = tonumber(hwnd),
            action = "wm_command",
            command_id = command_id,
            caption = caption,
        }
    end
    send_key(hwnd, VK_F10, 150)
    send_key(hwnd, string.byte("F"), 150)
    send_key(hwnd, string.byte("S"), 150)
    return {
        ok = true,
        hwnd = tonumber(hwnd),
        action = "menu_save_fallback",
    }
end
local ok_field_map, field_map_err = pcall(field_map.load, COMPONENT_ROOT)
if not ok_field_map then
    log.error("YDAgentServer: failed to load field map: " .. tostring(field_map_err))
end

local json = {}
json.null = {}

local function clear_apply_approvals()
    local ch = open_channel(APPLY_APPROVAL_CHANNEL)
    if not ch then
        return
    end
    while true do
        local ok = ch:pop()
        if not ok then
            break
        end
    end
end

local function consume_apply_approval()
    local ch = open_channel(APPLY_APPROVAL_CHANNEL)
    if not ch then
        return false
    end
    local ok, value = ch:pop()
    return ok and value == "approved"
end

local function publish_review_plan(plan)
    REVIEW_PUBLISHED = REVIEW_PUBLISHED + 1
    local ch = open_channel(REVIEW_CHANNEL)
    if ch then
        ch:push(json.encode(plan))
    end
end

local function clear_review_queue()
    local ch = open_channel(REVIEW_CHANNEL)
    if not ch then
        return
    end
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
    if type(p) == "string" then
        return p ~= "" and p or nil
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

local function normalize_map_key(path)
    if type(path) ~= "string" then
        return nil
    end
    path = path:gsub("/", "\\")
    path = path:gsub("^%s+", ""):gsub("%s+$", "")
    if path == "" then
        return nil
    end
    return path:lower()
end

local function pending_globals_path()
    if COMPONENT_ROOT == nil or COMPONENT_ROOT == "" then
        return "logs\\ydagent_pending_globals.lua"
    end
    return path_join(COMPONENT_ROOT, "logs\\ydagent_pending_globals.lua")
end

local function load_pending_global_overrides()
    local chunk = loadfile(pending_globals_path())
    if not chunk then
        return {}
    end
    local ok, data = pcall(chunk)
    if not ok or type(data) ~= "table" then
        return {}
    end
    return data
end

local function sorted_keys(tbl)
    local keys = {}
    for key in pairs(tbl or {}) do
        keys[#keys + 1] = key
    end
    table.sort(keys)
    return keys
end

local function save_pending_global_overrides(data)
    local path = pending_globals_path()
    if next(data or {}) == nil then
        os.remove(path)
        return true
    end
    local f = io.open(path, "wb")
    if not f then
        return nil, "cannot write pending globals: " .. tostring(path)
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

local GLOBAL_TYPE_IDS = {
    integer = 1,
    real = 2,
    boolean = 3,
    string = 4,
    timer = 5,
    trigger = 6,
    unit = 7,
    unitcode = 8,
    abilcode = 9,
    item = 10,
    itemcode = 11,
    group = 12,
    player = 13,
    location = 14,
    destructable = 15,
    force = 16,
    rect = 17,
    region = 18,
    sound = 19,
    effect = 20,
    unitpool = 21,
    itempool = 22,
    quest = 23,
    questitem = 24,
    timerdialog = 25,
    leaderboard = 26,
    multiboard = 27,
    multiboarditem = 28,
    trackable = 29,
    dialog = 30,
    button = 31,
    texttag = 32,
    lightning = 33,
    image = 34,
    fogstate = 35,
    fogmodifier = 36,
    radian = 37,
    degree = 38,
}

local function trim(s)
    return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

local DEFAULT_GLOBAL_OPTION_KEY = "Def   "

local function default_global_value_for_type(type_name)
    if type_name == "integer" then
        return "0"
    elseif type_name == "real" then
        return "0.0"
    elseif type_name == "boolean" then
        return "false"
    elseif type_name == "string" then
        return ""
    end
    return nil
end

local function normalize_global_name(name)
    if type(name) ~= "string" then
        return nil
    end
    name = trim(name)
    if name == "" then
        return nil
    end
    if name:match("^udg_[%a_][%w_]*$") then
        return name:sub(5)
    end
    return name
end

local function live_global_name(name)
    local normalized = normalize_global_name(name)
    if not normalized then
        return nil
    end
    return "udg_" .. normalized
end

local function native_global_count()
    return tonumber(YDT.ydt_get_global_count()) or 0
end

local function load_script_global_types()
    local f = io.open(path_join(COMPONENT_ROOT, "logs\\currentmapscript.j"), "rb")
    if not f then
        return {}
    end
    local types = {}
    local in_globals = false
    local in_init_globals = false
    for line in f:lines() do
        local stripped = line:gsub("//.*$", "")
        if stripped:match("^%s*globals%s*$") then
            in_globals = true
        elseif stripped:match("^%s*endglobals%s*$") then
            in_globals = false
        elseif in_globals then
            local value = stripped:match("=%s*(.-)%s*$")
            local decl = stripped:gsub("=%s*.-%s*$", "")
            local type_name, name = decl:match("^%s*constant%s+([%a_][%w_]*)%s+array%s+([%a_][%w_]*)")
            local is_array = type_name ~= nil
            if not type_name then
                type_name, name = decl:match("^%s*constant%s+([%a_][%w_]*)%s+([%a_][%w_]*)")
            end
            if not type_name then
                type_name, name = decl:match("^%s*([%a_][%w_]*)%s+array%s+([%a_][%w_]*)")
                is_array = type_name ~= nil
            end
            if not type_name then
                type_name, name = decl:match("^%s*([%a_][%w_]*)%s+([%a_][%w_]*)")
            end
            local type_id = type_name and GLOBAL_TYPE_IDS[type_name]
            if type_id and name then
                types[name] = {
                    id = type_id,
                    name = type_name,
                    array = is_array == true,
                    initial_value = value and trim(value) or nil,
                }
            end
        elseif stripped:match("^%s*function%s+InitGlobals%s+takes%s+nothing%s+returns%s+nothing%s*$") then
            in_init_globals = true
        elseif in_init_globals and stripped:match("^%s*endfunction%s*$") then
            in_init_globals = false
        elseif in_init_globals then
            local name, value = stripped:match("^%s*set%s+([%a_][%w_]*)%s*=%s*(.-)%s*$")
            if name and value and types[name] and not types[name].array then
                types[name].initial_value = trim(value)
            end
        end
    end
    f:close()
    return types
end

local function current_map_path()
    local f = io.open(path_join(COMPONENT_ROOT, "logs\\ydwe.log"), "rb")
    if not f then
        return nil, "ydwe.log not found"
    end
    local last = nil
    for line in f:lines() do
        local open_path = line:match("Open map\t(.+)$")
        if open_path and open_path ~= "" then
            last = open_path
        end
        local save_path = line:match("Saving%s+(.+)$")
        if save_path and save_path ~= "" then
            last = save_path
        end
    end
    f:close()
    if not last then
        return nil, "current map path not found in log"
    end
    last = last:gsub("[%z\001-\031]", "")
    last = trim(last:gsub("/", "\\"))
    return last
end

function editor.current_map_path()
    return current_map_path()
end

local function resolve_map_temp_dir(map_path)
    if type(map_path) ~= "string" or map_path == "" then
        map_path = current_map_path()
    end
    if type(map_path) ~= "string" or map_path == "" then
        return nil, "map path is required"
    end
    if map_path:match("%.w3x[Tt]emp[\\/]?$") then
        return map_path
    end
    return map_path .. "Temp"
end

local function read_all_lines(path)
    local f = io.open(path, "rb")
    if not f then
        return nil, "cannot open file: " .. tostring(path)
    end
    local lines = {}
    for line in f:lines() do
        lines[#lines + 1] = line
    end
    f:close()
    return lines
end

local function path_dirname(path)
    if type(path) ~= "string" then
        return nil
    end
    local dir = path:match("^(.*)[/\\][^/\\]+$")
    return dir
end

local function is_lni_map_marker(map_path)
    if type(map_path) ~= "string" or map_path == "" then
        return false
    end
    local marker = map_path:match("[/\\](%.[Ww]3[xmn])$")
    if not marker then
        return false
    end
    local dir = path_dirname(map_path)
    if not dir then
        return false
    end
    local f = io.open(path_join(dir, "trigger\\variable.lml"), "rb")
    if f then
        f:close()
        return true
    end
    return false
end

local function parse_variable_lml(path)
    local lines, err = read_all_lines(path)
    if not lines then
        return nil, err
    end
    local vars = {}
    local current = nil
    for _, raw_line in ipairs(lines) do
        if raw_line ~= "" then
            if raw_line:match("^%s%s%s%s") then
                if not current then
                    return nil, "orphan variable option in " .. tostring(path)
                end
                local inner = raw_line:gsub("^%s+", "")
                local key, value = inner:match("^([^:]+):%s*(.*)$")
                if key then
                    key = trim(key)
                    current.options[key] = value or ""
                    current.option_order[#current.option_order + 1] = key
                end
            else
                local name, type_name = raw_line:match("^([^:]+):%s*(.+)$")
                if not name or not type_name then
                    return nil, "invalid variable declaration: " .. tostring(raw_line)
                end
                current = {
                    name = trim(name),
                    type_name = trim(type_name),
                    options = {},
                    option_order = {},
                }
                vars[#vars + 1] = current
            end
        end
    end
    return vars
end

local function write_variable_lml(path, vars)
    local f = io.open(path, "wb")
    if not f then
        return nil, "cannot write file: " .. tostring(path)
    end
    for _, var in ipairs(vars or {}) do
        f:write(var.name, ": ", var.type_name, "\n")
        local emitted = {}
        for _, key in ipairs(var.option_order or {}) do
            if not emitted[key] and var.options[key] ~= nil then
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

local function get_variable_lml_path(map_path)
    if type(map_path) ~= "string" or map_path == "" then
        map_path = current_map_path()
    end
    if type(map_path) ~= "string" or map_path == "" then
        return nil, "map path is required"
    end
    if is_lni_map_marker(map_path) then
        local dir = path_dirname(map_path)
        if not dir then
            return nil, "cannot resolve lni map directory"
        end
        return path_join(dir, "trigger\\variable.lml")
    end
    local temp_dir, err = resolve_map_temp_dir(map_path)
    if not temp_dir then
        return nil, err
    end
    return path_join(temp_dir, "trigger\\variable.lml")
end

local function read_variable_file(map_path)
    local variable_path, err = get_variable_lml_path(map_path)
    if not variable_path then
        return nil, err
    end
    local vars, parse_err = parse_variable_lml(variable_path)
    if not vars then
        return nil, parse_err
    end
    return vars, variable_path
end

local function file_global_defs(map_path)
    local vars, err = read_variable_file(map_path)
    if not vars then
        return nil, err
    end
    local defs = {}
    for _, var in ipairs(vars) do
        defs[#defs + 1] = {
            name = live_global_name(var.name),
            type_name = var.type_name,
            type_id = GLOBAL_TYPE_IDS[var.type_name],
            array = tostring((var.options or {}).Array or "0") ~= "0",
        }
    end
    return defs
end

local function find_variable(vars, name)
    local normalized = normalize_global_name(name)
    if not normalized then
        return nil, nil
    end
    for index, var in ipairs(vars or {}) do
        if var.name == normalized then
            return var, index
        end
    end
    return nil, nil
end

local function ensure_default_option(var)
    local found = nil
    for _, key in ipairs(var.option_order or {}) do
        if key:match("^Def") then
            found = key
            break
        end
    end
    if not found then
        found = DEFAULT_GLOBAL_OPTION_KEY
        var.option_order = var.option_order or {}
        var.option_order[#var.option_order + 1] = found
    end
    return found
end

local function write_globals_file(map_path, vars)
    local variable_path, err = get_variable_lml_path(map_path)
    if not variable_path then
        return nil, err
    end
    return write_variable_lml(variable_path, vars)
end

local function file_global_value(name, map_path)
    local vars, err = read_variable_file(map_path)
    if not vars then
        return nil, err
    end
    local var = find_variable(vars, name)
    if not var then
        return nil
    end
    for _, key in ipairs(var.option_order or {}) do
        if key:match("^Def") then
            return var.options[key]
        end
    end
    for key, value in pairs(var.options or {}) do
        if key:match("^Def") then
            return value
        end
    end
    return nil
end

local function file_set_global(name, type_name, value, map_path)
    local vars, err = read_variable_file(map_path)
    if not vars then
        return nil, err
    end
    local normalized = normalize_global_name(name)
    if not normalized then
        return nil, "invalid global name"
    end
    local var = find_variable(vars, normalized)
    if not var then
        return nil, "global not found: " .. tostring(name)
    end
    if type_name and type_name ~= "" then
        var.type_name = type_name
    end
    local default_key = ensure_default_option(var)
    var.options[default_key] = tostring(value)
    return write_globals_file(map_path, vars)
end

local function stage_pending_global_override(map_path, name, type_name, value)
    local map_key = normalize_map_key(map_path)
    if not map_key then
        return nil, "map path is required"
    end
    local live_name = live_global_name(name) or name
    if not live_name then
        return nil, "invalid global name"
    end
    local data = load_pending_global_overrides()
    local entry = data[map_key]
    if type(entry) ~= "table" then
        entry = {}
        data[map_key] = entry
    end
    entry[live_name] = {
        type_name = type_name or "",
        value = tostring(value),
    }
    return save_pending_global_overrides(data)
end

local function file_create_global(name, type_name, value, map_path)
    local vars, err = read_variable_file(map_path)
    if not vars then
        return nil, err
    end
    local normalized = normalize_global_name(name)
    if not normalized then
        return nil, "invalid global name"
    end
    if type(type_name) ~= "string" or type_name == "" then
        return nil, "type_name is required"
    end
    local existing = find_variable(vars, normalized)
    if existing then
        return nil, "global already exists: " .. normalized
    end
    local default_value = value
    if default_value == nil then
        default_value = default_global_value_for_type(type_name)
    end
    vars[#vars + 1] = {
        name = normalized,
        type_name = type_name,
        options = {
            [DEFAULT_GLOBAL_OPTION_KEY] = default_value ~= nil and tostring(default_value) or "",
        },
        option_order = { DEFAULT_GLOBAL_OPTION_KEY },
    }
    return write_globals_file(map_path, vars)
end

local function file_delete_global(name, map_path)
    local vars, err = read_variable_file(map_path)
    if not vars then
        return nil, err
    end
    local normalized = normalize_global_name(name)
    if not normalized then
        return nil, "invalid global name"
    end
    for index, var in ipairs(vars) do
        if var.name == normalized then
            table.remove(vars, index)
            return write_globals_file(map_path, vars)
        end
    end
    return nil, "global not found: " .. normalized
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

function agent.create_trigger(name)
    return tonumber(YDT.ydt_create_trigger(name)) or 0
end

function agent.delete_trigger(idx)
    return YDT.ydt_delete_trigger(idx) ~= 0
end

function agent.global_count()
    local count = native_global_count()
    if count > 0 then
        return count
    end
    local map_path = current_map_path()
    if is_lni_map_marker(map_path) then
        local defs = file_global_defs(map_path)
        if defs then
            return #defs
        end
    end
    return 0
end

function agent.global_name(idx)
    local count = native_global_count()
    if idx >= 0 and idx < count then
        local name = to_str(YDT.ydt_get_global_name(idx))
        if name and name ~= "" then
            return name
        end
    end
    local map_path = current_map_path()
    if is_lni_map_marker(map_path) then
        local defs = file_global_defs(map_path)
        local entry = defs and defs[idx + 1]
        return entry and entry.name or nil
    end
    return nil
end

function agent.global_type(idx)
    local name = agent.global_name(idx)
    local info = name and load_script_global_types()[name]
    if info then
        return info.id
    end
    local map_path = current_map_path()
    if is_lni_map_marker(map_path) then
        local defs = file_global_defs(map_path)
        local entry = defs and defs[idx + 1]
        if entry and entry.type_id then
            return entry.type_id
        end
    end
    local count = native_global_count()
    if idx >= 0 and idx < count then
        local r = YDT.ydt_get_global_type(idx)
        return r >= 0 and r or nil
    end
    return nil
end

function agent.global_value(idx)
    local count = native_global_count()
    if idx >= 0 and idx < count then
        local val = to_str(YDT.ydt_get_global_value(idx))
        if val and val ~= "" then
            return val
        end
    end
    
    local name = agent.global_name(idx)
    local map_path = current_map_path()
    if name and is_lni_map_marker(map_path) then
        local file_val = file_global_value(name, map_path)
        if file_val ~= nil then
            return file_val
        end
    end
    local info = name and load_script_global_types()[name]
    if info and info.array then
        return nil
    end
    if info then
        return info.initial_value
    end
    return nil
end

function agent.set_global_value(idx, value)
    if rawget(_G, "YDAGENT_TEST_STUB") then
        return false
    end
    local name = agent.global_name(idx)
    if not name then
        return nil, "global not found: " .. tostring(idx)
    end
    local info = load_script_global_types()[name]
    if info and info.array then
        return nil, "array global write is not supported"
    end
    local type_name = info and info.name or nil
    local map_path = current_map_path()
    if is_lni_map_marker(map_path) then
        return file_set_global(name, type_name, value, map_path)
    end
    return stage_pending_global_override(map_path, name, type_name, value)
end

function agent.list_globals()
    local count = agent.global_count()
    local script_types = load_script_global_types()
    local list = {}
    for i = 0, count - 1 do
        local name = agent.global_name(i)
        local type_info = name and script_types[name]
        local array = nil
        if type_info ~= nil then
            array = type_info.array
        end
        local value = nil
        if not array then
            value = agent.global_value(i)
        end
        list[#list + 1] = {
            index = i,
            name = name,
            type = type_info and type_info.id or agent.global_type(i),
            type_name = type_info and type_info.name or nil,
            array = array,
            value = value,
        }
    end
    return list
end

function agent.global_diag()
    local raw = to_str(YDT.ydt_global_diag())
    if not raw then
        return nil
    end
    local parsed, err = json.decode(raw)
    if err then
        return { raw = raw, error = err }
    end
    return parsed
end

function agent.create_global(name, type_name, value)
    return file_create_global(name, type_name, value)
end

function agent.delete_global(name)
    return file_delete_global(name)
end

function agent.file_global_value(name, map_path)
    return file_global_value(name, map_path)
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

local function snapshot_operation(op, options)
    if op.op == "set_trigger_name" then
        return {
            target = "trigger",
            field = "name",
            trigger_index = op.trigger_index,
            before = agent.trigger_name(op.trigger_index),
            after = op.name,
        }
    elseif op.op == "set_trigger_disabled" then
        return {
            target = "trigger",
            field = "disabled",
            trigger_index = op.trigger_index,
            before = agent.trigger_disabled(op.trigger_index),
            after = op.disabled,
        }
    elseif op.op == "set_eca_func_name" then
        return {
            target = "eca",
            field = "func",
            trigger_index = op.trigger_index,
            eca_type = op.eca_type,
            eca_index = op.eca_index,
            before = agent.eca_func_name(op.trigger_index, op.eca_type, op.eca_index),
            after = op.func,
        }
    elseif op.op == "set_eca_active" then
        return {
            target = "eca",
            field = "active",
            trigger_index = op.trigger_index,
            eca_type = op.eca_type,
            eca_index = op.eca_index,
            after = op.active,
        }
    elseif op.op == "set_eca_param_value" then
        return {
            target = "eca_param",
            field = "value",
            trigger_index = op.trigger_index,
            eca_type = op.eca_type,
            eca_index = op.eca_index,
            param_index = op.param_index,
            before = agent.eca_param_value(op.trigger_index, op.eca_type, op.eca_index, op.param_index),
            after = op.value,
        }
    elseif op.op == "add_eca" then
        return {
            target = "eca",
            field = "count",
            trigger_index = op.trigger_index,
            eca_type = op.eca_type,
            before = agent.eca_count(op.trigger_index, op.eca_type),
            after = (agent.eca_count(op.trigger_index, op.eca_type) or 0) + 1,
        }
    elseif op.op == "remove_eca" then
        local count = agent.eca_count(op.trigger_index, op.eca_type) or 0
        return {
            target = "eca",
            field = "count",
            trigger_index = op.trigger_index,
            eca_type = op.eca_type,
            eca_index = op.eca_index,
            before = count,
            after = count > 0 and count - 1 or 0,
        }
    elseif op.op == "object_set_field" then
        local before_val = nil
        local map_path = options and options.map_path
        if map_path and map_path ~= "" then
            local data = object.read(op.type_name, map_path)
            if data then
                local decoded = json.decode(data)
                if decoded and decoded[op.record_kind] then
                    local rec = find_record(decoded[op.record_kind], op.object_id)
                    if rec and rec.fields then
                        before_val = rec.fields[op.field_id]
                    end
                end
            end
        end
        return {
            target = "object",
            field = op.field_id,
            type_name = op.type_name,
            record_kind = op.record_kind,
            object_id = op.object_id,
            before = before_val,
            after = op.value,
        }
    end
    return nil
end

local function revert_operation(op, snapshot, options)
    if not snapshot then return false end
    if op.op == "set_trigger_name" then
        return agent.set_trigger_name(op.trigger_index, snapshot.before)
    elseif op.op == "set_trigger_disabled" then
        if snapshot.before == nil then return false end
        return agent.set_trigger_disabled(op.trigger_index, snapshot.before)
    elseif op.op == "set_eca_func_name" then
        if snapshot.before == nil then return false end
        return agent.set_eca_func_name(op.trigger_index, op.eca_type, op.eca_index, snapshot.before)
    elseif op.op == "set_eca_active" then
        return agent.set_eca_active(op.trigger_index, op.eca_type, op.eca_index, true)
    elseif op.op == "set_eca_param_value" then
        if snapshot.before == nil then return false end
        return agent.set_eca_param_value(op.trigger_index, op.eca_type, op.eca_index, op.param_index, snapshot.before)
    elseif op.op == "add_eca" then
        if snapshot.before == nil then return false end
        return agent.remove_eca(op.trigger_index, op.eca_type, snapshot.before)
    elseif op.op == "remove_eca" then
        return false
    elseif op.op == "object_set_field" then
        local map_path = options and options.map_path
        if not map_path or map_path == "" then return false end
        local data = object.read(op.type_name, map_path)
        if not data then return false end
        local decoded = json.decode(data)
        if not decoded then return false end
        local records = decoded[op.record_kind]
        local rec = find_record(records, op.object_id)
        if not rec then return false end
        rec.fields = rec.fields or {}
        rec.fields[op.field_id] = snapshot.before
        return object.write(op.type_name, map_path, json.encode(decoded))
    end
    return false
end

local function verify_operation(op, before)
    if op.op == "set_trigger_name" then
        local after = agent.trigger_name(op.trigger_index)
        return after == op.name, after
    elseif op.op == "set_trigger_disabled" then
        local after = agent.trigger_disabled(op.trigger_index)
        return after == op.disabled, after
    elseif op.op == "set_eca_func_name" then
        local after = agent.eca_func_name(op.trigger_index, op.eca_type, op.eca_index)
        return after == op.func, after
    elseif op.op == "set_eca_param_value" then
        local after = agent.eca_param_value(op.trigger_index, op.eca_type, op.eca_index, op.param_index)
        return after == op.value, after
    elseif op.op == "add_eca" then
        local after = agent.eca_count(op.trigger_index, op.eca_type) or 0
        return after == ((before and before.before or 0) + 1), after
    elseif op.op == "remove_eca" then
        local after = agent.eca_count(op.trigger_index, op.eca_type) or 0
        return after == (before and before.after or after), after
    end
    return nil, nil
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
        result.preview = {}
        for index, op in ipairs(validation.operations) do
            result.preview[#result.preview + 1] = {
                index = index,
                op = op.op,
                snapshot = snapshot_operation(op, options),
            }
        end
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

    local rollbacks = {}
    for index, op in ipairs(validation.operations) do
        local snapshot = snapshot_operation(op, options)
        local ok, err = apply_operation(op, options)
        local verified, after = nil, nil
        if ok == true then
            verified, after = verify_operation(op, snapshot)
            rollbacks[#rollbacks + 1] = { op = op, snapshot = snapshot }
        end
        result.applied[#result.applied + 1] = {
            index = index,
            op = op.op,
            ok = ok == true,
            snapshot = snapshot,
            after = after,
            verified = verified,
            error = ok == true and nil or (err or "operation returned false"),
        }
        if ok ~= true and options.continue_on_error ~= true then
            result.error = err or "operation failed"
            result.rollback_results = {}
            for i = #rollbacks, 1, -1 do
                local rb = rollbacks[i]
                local rb_ok = revert_operation(rb.op, rb.snapshot, options)
                result.rollback_results[#result.rollback_results + 1] = {
                    op = rb.op.op,
                    ok = rb_ok == true,
                }
            end
            result.error = result.error .. " (rollback attempted)"
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

function ai_rpc.mem_dump(addr_str, size_str)
    local addr = tonumber(addr_str)
    if not addr then return nil, "invalid address" end
    local size = tonumber(size_str) or 256
    if size > 8192 then size = 8192 end
    
    if YDT.ydt_mem_dump then
        local cstr = YDT.ydt_mem_dump(addr, size)
        if cstr ~= nil then
            return ffi.string(cstr)
        end
    end
    return ""
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
        global_count = agent.global_count() or 0,
        global_diag = agent.global_diag(),
        review_published = REVIEW_PUBLISHED,
        ai = ai.status(),
        last_error = LAST_ERROR,
    }
end

function diag.smoke()
    local checks = {}
    local function check(name, fn)
        local ok, result = pcall(fn)
        local item_ok = ok and result ~= nil
        local item = {
            name = name,
            ok = item_ok,
        }
        if item_ok then
            item.result = result
        else
            item.error = tostring(result)
        end
        checks[#checks + 1] = item
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

    local target = namespace == "agent" and agent
        or namespace == "object" and object
        or namespace == "ai" and ai_rpc
        or namespace == "diag" and diag
        or namespace == "editor" and editor
        or nil
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
        result = result == nil and json.null or result,
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

local stop_channel = open_channel("ydagent_stop")
log.info("YDAgentServer: listening on 127.0.0.1:" .. tostring(PORT))

while true do
    if stop_channel then
        local stop_ok, stop_msg = stop_channel:pop()
        if stop_ok and stop_msg == "stop" then
            break
        end
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
