require 'bee'
local socket = require 'bee.socket'
local thread = require 'bee.thread'

local M = {}
local APPLY_APPROVAL_CHANNEL = "ydagent_apply_approval"
local REVIEW_CHANNEL = "ydagent_review_plan"
local review_items = {}
local rpc_id = 0

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

local function json_encode(v)
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
                parts[i] = json_encode(v[i])
            end
            return "[" .. table.concat(parts, ",") .. "]"
        end
        local i = 1
        for k, value in pairs(v) do
            parts[i] = '"' .. json_escape(tostring(k)) .. '":' .. json_encode(value)
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

local parse_value

parse_value = function()
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
        return nil
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

local function json_decode(s)
    decode_str = s or ""
    decode_pos = 1
    return parse_value()
end

local function send_all(client, data)
    local offset = 1
    while offset <= #data do
        local _, writable = socket.select(nil, { client }, 1)
        if not writable or not writable[1] then
            return nil, "send timeout"
        end
        local n = client:send(data:sub(offset))
        if not n then
            return nil, "send failed"
        end
        offset = offset + n
    end
    return true
end

local function recv_all(client)
    local data = ""
    local wait_count = 0
    while wait_count < 1200 do
        local readable = socket.select({ client }, nil, 0.1)
        if readable and readable[1] then
            local chunk = client:recv(8192)
            if chunk == nil then
                break
            elseif chunk ~= false then
                data = data .. chunk
                wait_count = 0
            end
        else
            wait_count = wait_count + 1
        end
    end
    return data
end

local function rpc_call(method, params)
    rpc_id = rpc_id + 1
    local client, ok = socket.connect('tcp', '127.0.0.1', 27118)
    if not client then
        return nil, tostring(ok)
    end
    if ok ~= true then
        socket.select(nil, { client }, 1)
    end

    local request = json_encode({
        jsonrpc = "2.0",
        id = rpc_id,
        method = method,
        params = params or {},
    })
    local sent, send_err = send_all(client, request)
    if not sent then
        client:close()
        return nil, send_err
    end
    local response = recv_all(client)
    client:close()
    if response == "" then
        return nil, "empty response"
    end
    return response
end

local function agent_available()
    return type(_G.ydwe_agent) == "table"
end

local function trigger_summary()
    local agent = _G.ydwe_agent
    if type(agent) ~= "table" then
        return "Agent Core: not loaded"
    end

    local count = agent.trigger_count and agent.trigger_count() or 0
    return string.format("Agent Core: loaded\nTrigger count: %s", tostring(count))
end

local function status_text()
    local lines = {
        "YDWE AI Agent",
        "",
        trigger_summary(),
        "",
        "JSON-RPC server: 127.0.0.1:27118",
        "",
        "Safe workflow:",
        "1. Build prompt with agent.build_prompt(index, instruction).",
        "2. Send prompt to ai.complete or an external model.",
        "3. Parse with ai.parse_response(provider, response_text).",
        "4. Preview with ai.apply_plan(plan, { dry_run = true }).",
        "5. Apply only after review using confirm=true and dry_run=false.",
    }
    return table.concat(lines, "\n")
end

local function schema_text()
    return table.concat({
        "Safe AI operations:",
        "",
        "set_trigger_name",
        "set_trigger_disabled",
        "set_eca_func_name",
        "set_eca_active",
        "set_eca_param_value",
        "add_eca",
        "remove_eca (requires review)",
        "object_set_field (requires review)",
        "",
        "AI changes are not applied automatically.",
        "The RPC caller must request dry-run preview first, then call confirm=true.",
    }, "\n")
end

local function push_apply_approval()
    thread.channel(APPLY_APPROVAL_CHANNEL):push("approved")
end

local function drain_review_queue()
    local ch = thread.channel(REVIEW_CHANNEL)
    while true do
        local ok, value = ch:pop()
        if not ok then
            break
        end
        if type(value) == "string" and value ~= "" then
            review_items[#review_items + 1] = value
            if #review_items > 20 then
                table.remove(review_items, 1)
            end
        end
    end
end

local function wrap_text(text)
    text = tostring(text or "")
    if #text > 12000 then
        text = text:sub(1, 12000) .. "\n... truncated ..."
    end

    local out = {}
    for line in text:gmatch("[^\r\n]+") do
        while #line > 110 do
            out[#out + 1] = line:sub(1, 110)
            line = line:sub(111)
        end
        out[#out + 1] = line
    end
    if #out == 0 then
        return ""
    end
    return table.concat(out, "\n")
end

local function latest_review_text()
    drain_review_queue()
    local latest = review_items[#review_items]
    if not latest then
        return "No queued AI review plan.\nUse ai.generate_plan(...), ai.generate_trigger_plan(...), or ai.queue_review(...) first."
    end
    return wrap_text(latest)
end

local function line_count(text)
    local _, n = tostring(text or ""):gsub("\n", "")
    return n + 1
end

function M.show_status(hwnd)
    gui.message(hwnd, "%s", status_text())
end

function M.show_schema(hwnd)
    gui.message(hwnd, "%s", schema_text())
end

function M.confirm_external_apply(hwnd)
    if not agent_available() then
        gui.error_message(hwnd, "%s", "YDAgentCore is not loaded.")
        return false
    end
    local approved = gui.yesno_message(hwnd, "%s", table.concat({
        "External AI clients can apply validated plans through ai.apply_plan.",
        "Only continue if you have reviewed the dry-run result.",
        "This grants one approval token for the next non-dry-run apply call.",
        "",
        "Allow external client to proceed with explicit confirm=true?",
    }, "\n"))
    if approved then
        push_apply_approval()
        gui.message(hwnd, "%s", "One AI apply approval token has been granted.")
    end
    return approved
end

function M.show_review_panel(hwnd)
    if not agent_available() then
        gui.error_message(hwnd, "%s", "YDAgentCore is not loaded.")
        return false
    end

    local yue = require 'yue.gui'
    local win = yue.Window.create {}
    win:sethasshadow(true)
    win:setresizable(true)
    win:setmaximizable(false)
    win:setminimizable(false)
    win:setalwaysontop(true)
    win:setcontentsize { width = 760, height = 560 }

    local root = yue.Container.create()
    root:setstyle { FlexDirection = 'column', Margin = 10 }
    win:setcontentview(root)

    local title = yue.Label.create('YDWE AI Review Panel')
    title:setstyle { Height = 28 }
    title:setfont(yue.Font.create('Segoe UI', 16, "bold", "normal"))
    root:addchildview(title)

    local status = yue.Label.create('')
    status:setstyle { Height = 24 }
    root:addchildview(status)

    local scroll = yue.Scroll.create()
    scroll:setstyle { FlexGrow = 1, MarginTop = 6, MarginBottom = 6 }
    scroll:setscrollbarpolicy('never', 'always')
    root:addchildview(scroll)

    local content = yue.Container.create()
    content:setstyle { FlexDirection = 'column' }
    scroll:setcontentview(content)

    local review = yue.Label.create('')
    review:setstyle { Margin = 4 }
    review:setalign 'start'
    content:addchildview(review)

    local buttons = yue.Container.create()
    buttons:setstyle { Height = 36, FlexDirection = 'row-reverse' }
    root:addchildview(buttons)

    local close = yue.Button.create('Close')
    close:setstyle { Width = 90, MarginLeft = 8 }
    buttons:addchildview(close)

    local approve = yue.Button.create('Approve Next Apply')
    approve:setstyle { Width = 150, MarginLeft = 8 }
    buttons:addchildview(approve)

    local refresh = yue.Button.create('Refresh')
    refresh:setstyle { Width = 90, MarginLeft = 8 }
    buttons:addchildview(refresh)

    local function update()
        local text = latest_review_text()
        review:settext(text)
        local height = math.max(440, line_count(text) * 18)
        review:setstyle { Margin = 4, Height = height, Width = 710 }
        scroll:setcontentsize { width = 720, height = height + 20 }
        status:settext(string.format("Queued review plans: %d", #review_items))
    end

    function refresh:onclick()
        update()
    end

    function approve:onclick()
        push_apply_approval()
        status:settext(string.format("Queued review plans: %d | one apply token granted", #review_items))
    end

    function close:onclick()
        win:close()
    end

    function win:onclose()
        yue.MessageLoop.quit()
    end

    update()
    win:center()
    win:activate()
    yue.MessageLoop.run()
    return true
end

function M.show_generate_panel(hwnd)
    if not agent_available() then
        gui.error_message(hwnd, "%s", "YDAgentCore is not loaded.")
        return false
    end

    local yue = require 'yue.gui'
    local win = yue.Window.create {}
    win:sethasshadow(true)
    win:setresizable(true)
    win:setmaximizable(false)
    win:setminimizable(false)
    win:setalwaysontop(true)
    win:setcontentsize { width = 820, height = 620 }

    local root = yue.Container.create()
    root:setstyle { FlexDirection = 'column', Margin = 10 }
    win:setcontentview(root)

    local title = yue.Label.create('YDWE AI Generate')
    title:setstyle { Height = 28 }
    title:setfont(yue.Font.create('Segoe UI', 16, "bold", "normal"))
    root:addchildview(title)

    local instruction = yue.Entry.create()
    instruction:setstyle { Height = 24, MarginBottom = 6 }
    instruction:settext('Explain or improve trigger logic safely')
    root:addchildview(instruction)

    local trigger_index = yue.Entry.create()
    trigger_index:setstyle { Height = 24, MarginBottom = 6 }
    trigger_index:settext('0')
    root:addchildview(trigger_index)

    local status = yue.Label.create('')
    status:setstyle { Height = 24 }
    root:addchildview(status)

    local scroll = yue.Scroll.create()
    scroll:setstyle { FlexGrow = 1, MarginTop = 6, MarginBottom = 6 }
    scroll:setscrollbarpolicy('never', 'always')
    root:addchildview(scroll)

    local content = yue.Container.create()
    content:setstyle { FlexDirection = 'column' }
    scroll:setcontentview(content)

    local output = yue.Label.create('')
    output:setstyle { Margin = 4, Width = 760, Height = 440 }
    output:setalign 'start'
    content:addchildview(output)

    local buttons = yue.Container.create()
    buttons:setstyle { Height = 72, FlexDirection = 'column' }
    root:addchildview(buttons)

    local row1 = yue.Container.create()
    row1:setstyle { Height = 36, FlexDirection = 'row-reverse' }
    buttons:addchildview(row1)

    local row2 = yue.Container.create()
    row2:setstyle { Height = 36, FlexDirection = 'row-reverse' }
    buttons:addchildview(row2)

    local close = yue.Button.create('Close')
    close:setstyle { Width = 90, MarginLeft = 8 }
    row1:addchildview(close)

    local review = yue.Button.create('Open Review')
    review:setstyle { Width = 110, MarginLeft = 8 }
    row1:addchildview(review)

    local explain = yue.Button.create('Explain Trigger')
    explain:setstyle { Width = 120, MarginLeft = 8 }
    row1:addchildview(explain)

    local gen_trigger = yue.Button.create('Generate Trigger Plan')
    gen_trigger:setstyle { Width = 150, MarginLeft = 8 }
    row1:addchildview(gen_trigger)

    local analyze = yue.Button.create('Analyze Relations')
    analyze:setstyle { Width = 130, MarginLeft = 8 }
    row2:addchildview(analyze)

    local gen_batch = yue.Button.create('Generate Batch Plan')
    gen_batch:setstyle { Width = 140, MarginLeft = 8 }
    row2:addchildview(gen_batch)

    local gen_rel = yue.Button.create('Relation Plan')
    gen_rel:setstyle { Width = 110, MarginLeft = 8 }
    row2:addchildview(gen_rel)

    local gen_code = yue.Button.create('Generate JASS')
    gen_code:setstyle { Width = 120, MarginLeft = 8 }
    row2:addchildview(gen_code)

    local function set_output(text)
        text = wrap_text(text)
        output:settext(text)
        local height = math.max(440, line_count(text) * 18)
        output:setstyle { Margin = 4, Height = height, Width = 760 }
        scroll:setcontentsize { width = 780, height = height + 20 }
    end

    local function call(method, params)
        status:settext('Running ' .. method .. ' ...')
        local result, err = rpc_call(method, params)
        if not result then
            status:settext('Error')
            set_output(err)
            return
        end
        status:settext('Done: ' .. method)
        set_output(result)
    end

    function gen_trigger:onclick()
        call('ai.generate_trigger_plan', {
            tonumber(trigger_index:gettext()) or 0,
            instruction:gettext(),
            { queue_review = true },
        })
    end

    function gen_batch:onclick()
        call('ai.generate_batch_plan', {
            instruction:gettext(),
            { trigger_limit = 20, node_limit = 20 },
            { queue_review = true },
        })
    end

    function explain:onclick()
        call('ai.explain_trigger', {
            tonumber(trigger_index:gettext()) or 0,
            {},
        })
    end

    function analyze:onclick()
        call('agent.analyze_relationships', {
            { trigger_limit = 50, node_limit = 30 },
        })
    end

    function gen_rel:onclick()
        call('ai.generate_relationship_plan', {
            instruction:gettext(),
            { trigger_limit = 50, node_limit = 30 },
            { queue_review = true },
        })
    end

    function gen_code:onclick()
        call('ai.generate_code', {
            'jass',
            instruction:gettext(),
            '',
            {},
        })
    end

    function review:onclick()
        M.show_review_panel(hwnd)
    end

    function close:onclick()
        win:close()
    end

    function win:onclose()
        yue.MessageLoop.quit()
    end

    set_output('Configure provider/model first with ai.configure, then use the buttons above.')
    win:center()
    win:activate()
    yue.MessageLoop.run()
    return true
end

return M
