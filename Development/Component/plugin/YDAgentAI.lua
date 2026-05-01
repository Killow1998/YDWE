require 'bee'
local subprocess = require 'bee.subprocess'
local ops = require 'YDAgentOps'

local M = {}

local config = {
    provider = "local",
    model = "",
    endpoint = "http://127.0.0.1:11434/api/chat",
    api_key_env = "",
    timeout = 60,
}

local json = nil

local PROVIDERS = {
    claude = {
        endpoint = "https://api.anthropic.com/v1/messages",
        api_key_env = "ANTHROPIC_API_KEY",
    },
    openai = {
        endpoint = "https://api.openai.com/v1/chat/completions",
        api_key_env = "OPENAI_API_KEY",
    },
    local_llm = {
        endpoint = "http://127.0.0.1:11434/api/chat",
        api_key_env = "",
    },
    local = {
        endpoint = "http://127.0.0.1:11434/api/chat",
        api_key_env = "",
    },
}

local function copy_table(t)
    local out = {}
    for k, v in pairs(t or {}) do
        out[k] = v
    end
    return out
end

local function get_env(name)
    if not name or name == "" then
        return nil
    end
    return os.getenv(name)
end

local function get_api_key(cfg)
    if cfg.api_key and cfg.api_key ~= "" then
        return cfg.api_key
    end
    return get_env(cfg.api_key_env)
end

local function normalize_messages(prompt, context)
    if type(prompt) == "table" then
        return prompt
    end
    local content = tostring(prompt or "")
    if context and context ~= "" then
        content = tostring(context) .. "\n\n" .. content
    end
    return {
        { role = "user", content = content },
    }
end

local function request_for_openai(cfg, messages)
    return {
        model = cfg.model,
        messages = messages,
        temperature = cfg.temperature or 0.2,
    }
end

local function request_for_claude(cfg, messages)
    local system = cfg.system or ""
    local converted = {}
    for _, msg in ipairs(messages) do
        if msg.role == "system" then
            system = msg.content or system
        else
            converted[#converted + 1] = {
                role = msg.role == "assistant" and "assistant" or "user",
                content = msg.content or "",
            }
        end
    end
    return {
        model = cfg.model,
        max_tokens = cfg.max_tokens or 2048,
        temperature = cfg.temperature or 0.2,
        system = system ~= "" and system or nil,
        messages = converted,
    }
end

local function request_for_local(cfg, messages)
    return {
        model = cfg.model,
        messages = messages,
        stream = false,
        options = {
            temperature = cfg.temperature or 0.2,
        },
    }
end

local function build_request(cfg, prompt, context, options)
    options = options or {}
    local messages = normalize_messages(prompt, context)
    local provider = cfg.provider
    local body
    local headers = {
        ["Content-Type"] = "application/json",
    }

    if provider == "openai" then
        body = request_for_openai(cfg, messages)
        local key = get_api_key(cfg)
        if key then
            headers["Authorization"] = "Bearer " .. key
        end
    elseif provider == "claude" then
        body = request_for_claude(cfg, messages)
        local key = get_api_key(cfg)
        if key then
            headers["x-api-key"] = key
        end
        headers["anthropic-version"] = cfg.anthropic_version or "2023-06-01"
    else
        body = request_for_local(cfg, messages)
    end

    for k, v in pairs(options.headers or {}) do
        headers[k] = v
    end

    return {
        provider = provider,
        endpoint = options.endpoint or cfg.endpoint,
        headers = headers,
        body = body,
        timeout = options.timeout or cfg.timeout,
    }
end

local function redacted(headers)
    local out = copy_table(headers)
    if out.Authorization then
        out.Authorization = "Bearer ***"
    end
    if out["x-api-key"] then
        out["x-api-key"] = "***"
    end
    return out
end

local function write_temp(data)
    local path = os.tmpname()
    local f = io.open(path, "wb")
    if not f then
        return nil, "failed to create temp request file"
    end
    f:write(data)
    f:close()
    return path
end

local function run_curl(req)
    if not json then
        return nil, "json encoder is not configured"
    end

    local body = json.encode(req.body)
    local body_path, err = write_temp(body)
    if not body_path then
        return nil, err
    end

    local args = {
        "curl.exe",
        "-sS",
        "--max-time", tostring(req.timeout or 60),
        "-X", "POST",
        req.endpoint,
        "--data-binary", "@" .. body_path,
        stdout = true,
        stderr = true,
        console = "disable",
    }

    for k, v in pairs(req.headers or {}) do
        args[#args + 1] = "-H"
        args[#args + 1] = k .. ": " .. tostring(v)
    end

    local process = subprocess.spawn(args)
    if not process then
        os.remove(body_path)
        return nil, "failed to start curl.exe"
    end

    local out = process.stdout:read 'a'
    local stderr = process.stderr:read 'a'
    local code = process:wait()
    os.remove(body_path)

    if code ~= 0 then
        return nil, stderr ~= "" and stderr or ("curl exited with " .. tostring(code))
    end
    return out
end

local function first_content_block(blocks)
    if type(blocks) ~= "table" then
        return nil
    end

    local parts = {}
    for _, block in ipairs(blocks) do
        if type(block) == "table" then
            if type(block.text) == "string" then
                parts[#parts + 1] = block.text
            elseif type(block.content) == "string" then
                parts[#parts + 1] = block.content
            end
        elseif type(block) == "string" then
            parts[#parts + 1] = block
        end
    end

    if #parts == 0 then
        return nil
    end
    return table.concat(parts, "\n")
end

local function extract_provider_text(provider, decoded)
    if type(decoded) ~= "table" then
        return nil
    end

    if provider == "openai" then
        local choice = decoded.choices and decoded.choices[1]
        if choice and choice.message then
            return choice.message.content
        end
        if choice then
            return choice.text
        end
    elseif provider == "claude" then
        return first_content_block(decoded.content)
    else
        if decoded.message and type(decoded.message.content) == "string" then
            return decoded.message.content
        end
        if type(decoded.response) == "string" then
            return decoded.response
        end
        if type(decoded.content) == "string" then
            return decoded.content
        end
    end

    return nil
end

local function strip_code_fence(text)
    local body = text:match("^%s*```[%w_%-]*%s*(.-)%s*```%s*$")
    return body or text
end

local function extract_json_text(text)
    if type(text) ~= "string" then
        return nil
    end

    text = strip_code_fence(text)
    local fenced = text:match("```json%s*(.-)%s*```")
    if fenced then
        return fenced
    end

    local first_obj = text:find("{", 1, true)
    local last_obj = text:match(".*()}")
    if first_obj and last_obj and last_obj >= first_obj then
        return text:sub(first_obj, last_obj)
    end

    local first_arr = text:find("[", 1, true)
    local last_arr = text:match(".*()%]")
    if first_arr and last_arr and last_arr >= first_arr then
        return text:sub(first_arr, last_arr)
    end

    return nil
end

local function decode_json_text(text)
    if not json then
        return nil, "json decoder is not configured"
    end

    local decoded, err = json.decode(text)
    if err then
        return nil, err
    end
    return decoded
end

local function parse_response(provider, response_text)
    local decoded = nil
    local decode_err = nil
    if type(response_text) == "string" and response_text ~= "" then
        decoded, decode_err = decode_json_text(response_text)
    end

    local text = nil
    if decoded then
        text = extract_provider_text(provider, decoded)
        if not text and (decoded.operations or decoded.actions or decoded.edits) then
            return {
                provider = provider,
                text = response_text,
                plan = decoded,
                validation = ops.validate_plan(decoded),
            }
        end
    else
        text = response_text
    end

    if not text or text == "" then
        return nil, decode_err or "AI response does not contain text"
    end

    local plan = nil
    local json_text = extract_json_text(text)
    if json_text then
        plan = decode_json_text(json_text)
    end

    return {
        provider = provider,
        text = text,
        plan = plan,
        validation = plan and ops.validate_plan(plan) or nil,
    }
end

function M.set_json(j)
    json = j
end

function M.configure(provider, options)
    provider = provider or "local"
    options = options or {}
    local base = PROVIDERS[provider]
    if not base then
        return nil, "unsupported provider: " .. tostring(provider)
    end

    config = copy_table(base)
    config.provider = provider
    for k, v in pairs(options) do
        config[k] = v
    end
    config.endpoint = config.endpoint or base.endpoint
    config.api_key_env = config.api_key_env or base.api_key_env
    config.timeout = tonumber(config.timeout) or 60
    return M.status()
end

function M.status()
    return {
        provider = config.provider,
        model = config.model,
        endpoint = config.endpoint,
        api_key_env = config.api_key_env,
        has_api_key = get_api_key(config) ~= nil,
        timeout = config.timeout,
    }
end

function M.build(prompt, context, options)
    local req = build_request(config, prompt, context, options)
    return {
        provider = req.provider,
        endpoint = req.endpoint,
        headers = redacted(req.headers),
        body = req.body,
        timeout = req.timeout,
    }
end

function M.complete(prompt, context, options)
    options = options or {}
    if not config.model or config.model == "" then
        return nil, "ai model is not configured"
    end
    if (config.provider == "openai" or config.provider == "claude") and not get_api_key(config) then
        return nil, "missing api key; set " .. tostring(config.api_key_env) .. " or configure api_key"
    end

    local req = build_request(config, prompt, context, options)
    if options.dry_run == true then
        return M.build(prompt, context, options)
    end
    local response, err = run_curl(req)
    if not response then
        return nil, err
    end
    if options.parse == true then
        return parse_response(config.provider, response)
    end
    return response
end

function M.generate_plan(prompt, context, options)
    options = options or {}
    options.parse = true
    local result, err = M.complete(prompt, context, options)
    if not result then
        return nil, err
    end
    if not result.validation then
        return nil, "AI response does not contain a valid operation plan"
    end
    return result
end

function M.generate_text(prompt, context, options)
    options = options or {}
    options.parse = false
    return M.complete(prompt, context, options)
end

function M.parse_response(provider, response_text)
    provider = provider or config.provider
    return parse_response(provider, response_text)
end

function M.validate_plan(plan)
    return ops.validate_plan(plan)
end

function M.operation_schema()
    return ops.schema()
end

M.configure("local", {})

return M
