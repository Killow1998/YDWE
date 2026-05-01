-- ydwe_agent_server_worker: WinSock2 JSON-RPC TCP server (runs in background thread)
-- Spawned by YDAgentServer.lua via thread.thread('ydwe_agent_server_worker')
-- No bee.socket dependency — uses raw WinSock2 FFI for YDWE 1.32.13 compatibility

local ffi    = require "ffi"
local log    = require "log"

local PORT = 27118

ffi.cdef([[
    typedef unsigned int SOCKET;
    typedef struct { unsigned short sa_family; char sa_data[14]; } sockaddr;
    typedef struct { short sin_family; unsigned short sin_port; unsigned int sin_addr; char sin_zero[8]; } sockaddr_in;
    int WSAStartup(unsigned short ver, void* data);
    int WSACleanup(void);
    SOCKET socket(int af, int type, int protocol);
    int bind(SOCKET s, sockaddr* name, int namelen);
    int listen(SOCKET s, int backlog);
    SOCKET accept(SOCKET s, sockaddr* addr, int* addrlen);
    int closesocket(SOCKET s);
    int recv(SOCKET s, char* buf, int len, int flags);
    int send(SOCKET s, const char* buf, int len, int flags);
    unsigned short htons(unsigned short hostshort);
    typedef struct { unsigned int fd_count; SOCKET fd_array[64]; } fd_set;
    int select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, int* timeout_sec);
    int WSAGetLastError(void);
]])

ffi.cdef([[
    int  ydt_get_trigger_count(void);
    const char* ydt_get_trigger_name(int i);
    int  ydt_get_trigger_disabled(int i);
    int  ydt_get_eca_count(int i, int t);
    const char* ydt_get_eca_func_name(int i, int t, int ei);
    int  ydt_get_eca_gui_id(int i, int t, int ei);
    int  ydt_get_eca_param_count(int i, int t, int ei);
    const char* ydt_get_eca_param_value(int i, int t, int ei, int p);
    int  ydt_set_trigger_name(int i, const char* n);
    int  ydt_set_trigger_disabled(int i, int d);
    int  ydt_set_eca_func_name(int i, int t, int ei, const char* n);
    int  ydt_set_eca_active(int i, int t, int ei, int a);
    int  ydt_set_eca_param_value(int i, int t, int ei, int p, const char* v);
    int  ydt_add_eca(int i, int t);
    int  ydt_remove_eca(int i, int t, int ei);
    const char* ydt_read_object_file(const char* path);
    int  ydt_write_object_file(const char* path, const char* json);
]])

----------------------------------------------------------------------
-- Minimal JSON
----------------------------------------------------------------------
local function json_escape(s)
    return (s:gsub('[%c\\\"]', {['\b']='\\b',['\f']='\\f',['\n']='\\n',['\r']='\\r',['\t']='\\t',['\\']='\\\\',['\"']='\\"'}))
end

local function json_encode(v)
    local t = type(v)
    if t == "nil" then return "null"
    elseif t == "boolean" then return v and "true" or "false"
    elseif t == "number" then return tostring(v)
    elseif t == "string" then return '"' .. json_escape(v) .. '"'
    elseif t == "table" then
        local ia, mk = true, 0
        for k in pairs(v) do if type(k)~="number" or k<1 or k~=math.floor(k) then ia=false; break end; if k>mk then mk=k end end
        if ia then for i=1,mk do if v[i]==nil then ia=false; break end end end
        local p = {}
        if ia then for i=1,mk do p[i]=json_encode(v[i]) end; return "["..table.concat(p,",").."]" end
        local i=1; for k,vv in pairs(v) do p[i]='"'..json_escape(tostring(k))..'":'..json_encode(vv); i=i+1 end
        return "{"..table.concat(p,",").."}"
    end; return "null"
end

local function json_decode(str)
    if type(str)~="string" or str=="" then return nil end
    local pos = 1
    local function skip() while pos<=#str do local c=str:sub(pos,pos); if c~=' ' and c~='\t' and c~='\n' and c~='\r' then break end; pos=pos+1 end end
    local function parse()
        skip(); if pos>#str then return nil end
        local c=str:sub(pos,pos)
        if c=='{' then pos=pos+1; local o={}; skip()
            if str:sub(pos,pos)=='}' then pos=pos+1; return o end
            while true do skip()
                if str:sub(pos,pos)~='"' then return nil end; pos=pos+1
                local key=""; while pos<=#str do local cc=str:sub(pos,pos)
                    if cc=='"' then pos=pos+1; break end
                    if cc=='\\' then pos=pos+1; cc=str:sub(pos,pos) end
                    key=key..cc; pos=pos+1 end
                skip(); if str:sub(pos,pos)~=':' then return nil end; pos=pos+1
                o[key]=parse(); skip()
                local sep=str:sub(pos,pos)
                if sep==',' then pos=pos+1 elseif sep=='}' then pos=pos+1; return o else return nil end
            end
        elseif c=='[' then pos=pos+1; local a={}; skip()
            if str:sub(pos,pos)==']' then pos=pos+1; return a end
            while true do a[#a+1]=parse(); skip()
                local sep=str:sub(pos,pos)
                if sep==',' then pos=pos+1 elseif sep==']' then pos=pos+1; return a else return nil end
            end
        elseif c=='"' then pos=pos+1; local s={}
            while pos<=#str do local cc=str:sub(pos,pos)
                if cc=='"' then pos=pos+1; return table.concat(s) end
                if cc=='\\' then pos=pos+1; cc=str:sub(pos,pos) end
                s[#s+1]=cc; pos=pos+1 end
            return nil
        elseif c=='t' and str:sub(pos,pos+3)=='true' then pos=pos+4; return true
        elseif c=='f' and str:sub(pos,pos+4)=='false' then pos=pos+5; return false
        elseif c=='n' and str:sub(pos,pos+3)=='null' then pos=pos+4; return nil
        elseif c=='-' or (c>='0' and c<='9') then local ns=str:match("^(%-?%d+%.?%d*[eE]?[%+%-]?%d*)",pos); if ns then pos=pos+#ns; return tonumber(ns) end end
        return nil
    end
    local v = parse(); skip()
    if pos<=#str then return nil end; return v
end

----------------------------------------------------------------------
-- Agent wrappers (call YDTrigger.dll directly via FFI)
----------------------------------------------------------------------
local YDT = ffi.load("YDTrigger.dll")
if not YDT then log.error("Worker: cannot load YDTrigger.dll"); return end

local function to_str(p)
    if p==nil then return nil end; local s=ffi.string(p); if s=="" then return nil end; return s
end

local function read_eca_list(i,t)
    local n=YDT.ydt_get_eca_count(i,t) or 0; if n==0 then return {} end
    local list={}
    for j=0,n-1 do
        local fn=to_str(YDT.ydt_get_eca_func_name(i,t,j))
        if fn then
            local node={func=fn,gui_id=YDT.ydt_get_eca_gui_id(i,t,j),params={}}
            local pc=YDT.ydt_get_eca_param_count(i,t,j) or 0
            for p=0,pc-1 do node.params[p+1]=to_str(YDT.ydt_get_eca_param_value(i,t,j,p)) end
            list[#list+1]=node
        end
    end; return list
end

local function dispatch(method, params)
    local args = params or {}
    if method=="agent.refresh" then local n=YDT.ydt_refresh(); return n>0 and n or nil
    elseif method=="agent.trigger_count" then return tonumber(YDT.ydt_get_trigger_count()) or 0
    elseif method=="agent.trigger_name" then return to_str(YDT.ydt_get_trigger_name(args[1]))
    elseif method=="agent.trigger_disabled" then local r=YDT.ydt_get_trigger_disabled(args[1]); return r>=0 and(r~=0)or nil
    elseif method=="agent.eca_count" then return tonumber(YDT.ydt_get_eca_count(args[1],args[2]))or 0
    elseif method=="agent.eca_func_name" then return to_str(YDT.ydt_get_eca_func_name(args[1],args[2],args[3]))
    elseif method=="agent.eca_gui_id" then local r=YDT.ydt_get_eca_gui_id(args[1],args[2],args[3]); return r>=0 and r or nil
    elseif method=="agent.eca_param_count" then return tonumber(YDT.ydt_get_eca_param_count(args[1],args[2],args[3]))or 0
    elseif method=="agent.eca_param_value" then return to_str(YDT.ydt_get_eca_param_value(args[1],args[2],args[3],args[4]))
    elseif method=="agent.list_triggers" then
        local list,n={},YDT.ydt_get_trigger_count()or 0
        for i=0,n-1 do list[#list+1]={name=to_str(YDT.ydt_get_trigger_name(i)),disabled=YDT.ydt_get_trigger_disabled(i)>=0 and YDT.ydt_get_trigger_disabled(i)~=0,
            event_count=YDT.ydt_get_eca_count(i,0)or 0,condition_count=YDT.ydt_get_eca_count(i,1)or 0,action_count=YDT.ydt_get_eca_count(i,2)or 0} end
        return list
    elseif method=="agent.get_eca_tree" then
        local i=args[1]; if i<0 or i>=(YDT.ydt_get_trigger_count()or 0) then return nil end
        return {events=read_eca_list(i,0),conditions=read_eca_list(i,1),actions=read_eca_list(i,2)}
    elseif method=="agent.dump_all" then
        local d,n={},YDT.ydt_get_trigger_count()or 0
        for i=0,n-1 do local e={name=to_str(YDT.ydt_get_trigger_name(i)),disabled=YDT.ydt_get_trigger_disabled(i)>=0 and YDT.ydt_get_trigger_disabled(i)~=0}
            local t={events=read_eca_list(i,0),conditions=read_eca_list(i,1),actions=read_eca_list(i,2)}
            e.events=t.events; e.conditions=t.conditions; e.actions=t.actions; d[#d+1]=e end
        return d
    elseif method=="agent.set_trigger_name" then return YDT.ydt_set_trigger_name(args[1],args[2])~=0
    elseif method=="agent.set_trigger_disabled" then return YDT.ydt_set_trigger_disabled(args[1],args[2]and 1 or 0)~=0
    elseif method=="agent.set_eca_func_name" then return YDT.ydt_set_eca_func_name(args[1],args[2],args[3],args[4])~=0
    elseif method=="agent.set_eca_active" then return YDT.ydt_set_eca_active(args[1],args[2],args[3],args[4]and 1 or 0)~=0
    elseif method=="agent.set_eca_param_value" then return YDT.ydt_set_eca_param_value(args[1],args[2],args[3],args[4],args[5])~=0
    elseif method=="agent.add_eca" then return YDT.ydt_add_eca(args[1],args[2])~=0
    elseif method=="agent.remove_eca" then return YDT.ydt_remove_eca(args[1],args[2],args[3])~=0
    end
    return nil, -32601, "Method not found: "..method
end

local function handle(data)
    local req = json_decode(data)
    if type(req)~="table" or not req.method then
        return json_encode({jsonrpc="2.0",id=nil,error={code=-32600,message="Invalid Request"}})
    end
    local ok, result = pcall(dispatch, req.method, req.params or {})
    if not ok then
        return json_encode({jsonrpc="2.0",id=req.id,error={code=-32603,message=tostring(result)}})
    end
    if type(result)=="table" and result[1]==nil and result[2]~=nil then -- error tuple
        return json_encode({jsonrpc="2.0",id=req.id,error={code=result[2],message=result[3]}})
    end
    return json_encode({jsonrpc="2.0",id=req.id,result=result})
end

----------------------------------------------------------------------
-- TCP Server (WinSock2)
----------------------------------------------------------------------
local wsadata = ffi.new("char[400]")
ffi.C.WSAStartup(0x0202, wsadata)
local s = ffi.C.socket(2,1,0) -- AF_INET=2, SOCK_STREAM=1
local addr = ffi.new("sockaddr_in")
addr.sin_family=2; addr.sin_port=ffi.C.htons(PORT); addr.sin_addr=0x0100007f
ffi.C.bind(s, ffi.cast("sockaddr*",addr), ffi.sizeof(addr))
ffi.C.listen(s, 5)
log.info("AgentWorker: listening on 127.0.0.1:"..PORT)

local fdset = ffi.new("fd_set")
local tmo = ffi.new("int[1]",1)

while true do
    fdset.fd_count=1; fdset.fd_array[0]=s
    local r = ffi.C.select(0, fdset, nil, nil, tmo)
    if r > 0 then
        local ca = ffi.new("sockaddr_in")
        local al = ffi.new("int[1]", ffi.sizeof(ca))
        local client = ffi.C.accept(s, ffi.cast("sockaddr*",ca), al)
        if client ~= -1 then
            local buf = ffi.new("char[131072]")
            local total = ffi.C.recv(client, buf, 131071, 0)
            if total > 0 then
                local data = ffi.string(buf, total)
                local resp = handle(data)
                ffi.C.send(client, resp, #resp, 0)
            end
            ffi.C.closesocket(client)
        end
    end
end
