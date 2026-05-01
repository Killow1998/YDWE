-- YDAgentServer: File-based JSON-RPC IPC (no threads/sockets needed)
-- External agent writes request to logs/agent.in.json
-- Server reads, processes, writes response to logs/agent.out.json
local log = require "log"
local ffi = require "ffi"

ffi.cdef([[
    int ydt_get_trigger_count(void);
    const char* ydt_get_trigger_name(int);
    int ydt_get_trigger_disabled(int);
    int ydt_get_eca_count(int,int);
    const char* ydt_get_eca_func_name(int,int,int);
    int ydt_get_eca_gui_id(int,int,int);
    int ydt_get_eca_param_count(int,int,int);
    const char* ydt_get_eca_param_value(int,int,int,int);
    int ydt_set_trigger_name(int,const char*);
    int ydt_set_trigger_disabled(int,int);
    int ydt_set_eca_param_value(int,int,int,int,const char*);
    int ydt_add_eca(int,int);
    int ydt_remove_eca(int,int,int);
    int ydt_create_trigger(const char*);
    const char* ydt_read_object_file(const char*);
    int ydt_write_object_file(const char*,const char*);
]])

local function ts(p)
    if p == nil then return nil end
    local s = ffi.string(p)
    if s == "" then return nil end
    return s
end

local function je(s)
    return (s:gsub('[%c\\\"]',{['\b']='\\b',['\f']='\\f',['\n']='\\n',['\r']='\\r',['\t']='\\t',['\\']='\\\\',['\"']='\\"'}))
end
local function jev(v)
    local t=type(v)
    if t=="nil" then return"null"
    elseif t=="boolean" then return v and"true"or"false"
    elseif t=="number" then return tostring(v)
    elseif t=="string" then return'"'..je(v)..'"'
    elseif t=="table" then
        local ia,mk=true,0;for k in pairs(v) do if type(k)~="number"or k<1 or k~=math.floor(k) then ia=false;break end;if k>mk then mk=k end end
        if ia then for i=1,mk do if v[i]==nil then ia=false;break end end end
        local p={};if ia then for i=1,mk do p[i]=jev(v[i]) end;return"["..table.concat(p,",").."]" end
        local i=1;for k,vv in pairs(v) do p[i]='"'..je(tostring(k))..'":'..jev(vv);i=i+1 end;return"{"..table.concat(p,",").."}"
    end;return"null"
end

local LOG_DIR = "Q:/AppData/ydwe/YDWE/Development/Component/logs"

-- Simple JSON array decoder: ["method", arg1, arg2, ...]
local function jd_arr(s)
    if type(s)~="string"or s=="" then return nil end
    local pos=1
    local function sk() while pos<=#s do local c=s:sub(pos,pos);if c~=' 'and c~='\t'and c~='\n'and c~='\r'then break end;pos=pos+1 end end
    local function pv()
        sk();local c=s:sub(pos,pos)
        if c=='"' then pos=pos+1;local out={}
            while pos<=#s do local cc=s:sub(pos,pos);if cc=='"' then pos=pos+1;return table.concat(out) end
                if cc=='\\' then pos=pos+1;cc=s:sub(pos,pos) end;out[#out+1]=cc;pos=pos+1 end
        elseif c=='[' then pos=pos+1;local a={};sk()
            if s:sub(pos,pos)==']' then pos=pos+1;return a end
            while true do a[#a+1]=pv();sk();local sep=s:sub(pos,pos)
                if sep==',' then pos=pos+1 elseif sep==']' then pos=pos+1;return a else return nil end end
        elseif c=='-'or(c>='0'and c<='9') then local ns=s:match("^(%-?%d+%.?%d*)",pos);if ns then pos=pos+#ns;return tonumber(ns) end
        elseif s:sub(pos,pos+3)=='true' then pos=pos+4;return true
        elseif s:sub(pos,pos+4)=='false' then pos=pos+5;return false
        elseif s:sub(pos,pos+3)=='null' then pos=pos+4;return nil end
        return nil
    end
    local v=pv();sk();if pos<=#s then return nil end
    if type(v)=="table" and type(v[1])=="string" then return v end
    return nil
end

local function handle_request(dll)
    local in_path = LOG_DIR .. "\\agent.in.json"
    local raw_open = io.__open or io.open
    local f = raw_open(in_path, "r")
    if not f then return end
    local data = f:read("*a")
    f:close()
    if not data or data == "" then return end

    local req = jd_arr(data)
    if not req then os.remove(in_path); return end

    local method = req[1]
    local result
    local ok2, err2 = pcall(function()
        if method == "list_triggers" then
            local n = dll.ydt_get_trigger_count()
            local list={}
            for i=0,n-1 do list[#list+1]={index=i,name=ts(dll.ydt_get_trigger_name(i)),
                disabled=dll.ydt_get_trigger_disabled(i)~=0,
                events=dll.ydt_get_eca_count(i,0)or 0,
                conditions=dll.ydt_get_eca_count(i,1)or 0,
                actions=dll.ydt_get_eca_count(i,2)or 0} end
            result=list
        elseif method == "dump_all" then
            local n = dll.ydt_get_trigger_count()
            local dump={}
            for i=0,n-1 do
                local e={index=i,name=ts(dll.ydt_get_trigger_name(i)),disabled=dll.ydt_get_trigger_disabled(i)~=0}
                for t=0,2 do local tn=t==0 and"events"or t==1 and"conditions"or"actions";e[tn]={}
                    local ec=dll.ydt_get_eca_count(i,t)or 0
                    for j=0,ec-1 do e[tn][j+1]={func=ts(dll.ydt_get_eca_func_name(i,t,j)),
                        gui_id=dll.ydt_get_eca_gui_id(i,t,j),
                        params={}}
                        local pc=dll.ydt_get_eca_param_count(i,t,j)or 0
                        for p=0,pc-1 do e[tn][j+1].params[p+1]=ts(dll.ydt_get_eca_param_value(i,t,j,p)) end
                    end end
                dump[#dump+1]=e end
            result=dump
        elseif method == "set_trigger_name" then
            result = dll.ydt_set_trigger_name(req[2], req[3]) ~= 0
        elseif method == "set_trigger_disabled" then
            result = dll.ydt_set_trigger_disabled(req[2], req[3] and 1 or 0) ~= 0
        elseif method == "set_eca_param_value" then
            result = dll.ydt_set_eca_param_value(req[2], req[3], req[4], req[5], req[6]) ~= 0
        elseif method == "add_eca" then
            result = dll.ydt_add_eca(req[2], req[3]) ~= 0
        elseif method == "remove_eca" then
            result = dll.ydt_remove_eca(req[2], req[3], req[4]) ~= 0
        elseif method == "create_trigger" then
            result = dll.ydt_create_trigger(req[2])
        elseif method == "delete_trigger" then
            result = dll.ydt_delete_trigger(req[2]) ~= 0
        elseif method == "set_eca_func_name" then
            result = dll.ydt_set_eca_func_name(req[2], req[3], req[4], req[5]) ~= 0
        elseif method == "set_eca_active" then
            result = dll.ydt_set_eca_active(req[2], req[3], req[4], req[5] and 1 or 0) ~= 0
        elseif method == "refresh" then
            result = dll.ydt_get_trigger_count()
        else
            result = {error="unknown method: "..method}
        end
    end)

    local out_path = LOG_DIR .. "\\agent.out.json"
    local fo = raw_open(out_path, "w")
    if fo then
        if ok2 then
            fo:write(jev({ok=true, result=result}))
        else
            fo:write(jev({ok=false, error=tostring(err2)}))
        end
        fo:close()
    end
    log.info(string.format("AgentServer: %s -> %s", method, ok2 and "ok" or "error"))
    os.remove(in_path)
end

local loader = {}
loader.load = function(path)
    log.info("YDAgentServer: file IPC ready. Write to logs/agent.in.json")

    local dll = ffi.load("YDTrigger.dll")
    if not dll then log.error("AgentServer: no DLL"); return false end

    local ok, ev = pcall(require, "ev")
    if ok and ev then
        ev.on('编译地图', function(success)
            if success then
                handle_request(dll)
            end
        end)
        log.info("AgentServer: will process requests on compilation")
    end

    return true
end
loader.unload = function() end
return loader
