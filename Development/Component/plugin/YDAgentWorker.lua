-- YDAgentWorker: Minimal TCP server running in bee.thread
-- Spawned by YDAgentServer.lua via bee.thread.thread("YDAgentWorker")

local function start_server()
    local f = io.open("Q:/AppData/ydwe/YDWE/Development/Component/logs/worker_boot.txt", "w")
    f:write("worker boot started\n"); f:flush()

    local ok_socket, socket = pcall(require, "bee.socket")
    if not ok_socket then f:write("require bee.socket failed: "..tostring(socket).."\n"); f:close(); return end
    f:write("bee.socket loaded\n"); f:flush()

    local ok_ffi, ffi = pcall(require, "ffi")
    if not ok_ffi then f:write("require ffi failed\n"); f:close(); return end
    f:write("ffi loaded\n"); f:flush()

    ffi.cdef([[
        int ydt_get_trigger_count(void); const char* ydt_get_trigger_name(int);
        int ydt_get_trigger_disabled(int); int ydt_get_eca_count(int,int);
        const char* ydt_get_eca_func_name(int,int,int); int ydt_get_eca_param_count(int,int,int);
        const char* ydt_get_eca_param_value(int,int,int,int);
        int ydt_set_trigger_name(int,const char*); int ydt_set_trigger_disabled(int,int);
        int ydt_set_eca_param_value(int,int,int,int,const char*);
        int ydt_add_eca(int,int); int ydt_remove_eca(int,int,int);
        const char* ydt_read_object_file(const char*); int ydt_write_object_file(const char*,const char*);
    ]])

    local YDT = ffi.load("YDTrigger.dll")
    if not YDT then f:write("YDTrigger load failed\n"); f:close(); return end
    f:write("YDTrigger loaded\n"); f:flush()

    local function ts(p)
        if p==nil then return nil end; local s=ffi.string(p); return s=="" and nil or s
    end

    local function js(s)
        return (s:gsub('[%c\\\"]',{['\b']='\\b',['\f']='\\f',['\n']='\\n',['\r']='\\r',['\t']='\\t',['\\']='\\\\',['\"']='\\"'}))
    end

    local function je(v)
        local t=type(v)
        if t=="nil" then return"null" elseif t=="boolean" then return v and"true"or"false"
        elseif t=="number" then return tostring(v) elseif t=="string" then return'"'..js(v)..'"'
        elseif t=="table" then
            local ia,mk=true,0;for k in pairs(v) do if type(k)~="number"or k<1 or k~=math.floor(k) then ia=false;break end;if k>mk then mk=k end end
            if ia then for i=1,mk do if v[i]==nil then ia=false;break end end end
            local p={};if ia then for i=1,mk do p[i]=je(v[i]) end;return"["..table.concat(p,",").."]" end
            local i=1;for k,vv in pairs(v) do p[i]='"'..js(tostring(k))..'":'..je(vv);i=i+1 end;return"{"..table.concat(p,",").."}"
        end;return"null"
    end

    local function jd(s)
        if type(s)~="string"or s=="" then return nil end;local pos=1
        local function sk() while pos<=#s do local c=s:sub(pos,pos);if c~=' 'and c~='\t'and c~='\n'and c~='\r'then break end;pos=pos+1 end end
        local function pv()
            sk();if pos>#s then return nil end;local c=s:sub(pos,pos)
            if c=='{' then pos=pos+1;local o={};sk()
                if s:sub(pos,pos)=='}' then pos=pos+1;return o end
                while true do sk();if s:sub(pos,pos)~='"' then return nil end;pos=pos+1;local k=""
                    while pos<=#s do local cc=s:sub(pos,pos);if cc=='"' then pos=pos+1;break end
                        if cc=='\\' then pos=pos+1;cc=s:sub(pos,pos) end;k=k..cc;pos=pos+1 end
                    sk();if s:sub(pos,pos)~=':' then return nil end;pos=pos+1;o[k]=pv();sk()
                    local sep=s:sub(pos,pos);if sep==',' then pos=pos+1 elseif sep=='}' then pos=pos+1;return o else return nil end end
            elseif c=='[' then pos=pos+1;local a={};sk()
                if s:sub(pos,pos)==']' then pos=pos+1;return a end
                while true do a[#a+1]=pv();sk();local sep=s:sub(pos,pos)
                    if sep==',' then pos=pos+1 elseif sep==']' then pos=pos+1;return a else return nil end end
            elseif c=='"' then pos=pos+1;local out={}
                while pos<=#s do local cc=s:sub(pos,pos);if cc=='"' then pos=pos+1;return table.concat(out) end
                    if cc=='\\' then pos=pos+1;cc=s:sub(pos,pos) end;out[#out+1]=cc;pos=pos+1 end;return nil
            elseif c=='t'and s:sub(pos,pos+3)=='true' then pos=pos+4;return true
            elseif c=='f'and s:sub(pos,pos+4)=='false' then pos=pos+5;return false
            elseif c=='n'and s:sub(pos,pos+3)=='null' then pos=pos+4;return nil
            elseif c=='-'or(c>='0'and c<='9') then local ns=s:match("^(%-?%d+%.?%d*[eE]?[%+%-]?%d*)",pos);if ns then pos=pos+#ns;return tonumber(ns) end end
            return nil
        end;local v=pv();sk();if pos<=#s then return nil end;return v
    end

    local function dispatch(m,args)
        args=args or{}
        if m=="agent.refresh" then local n=YDT.ydt_refresh();return n>0 and n or nil
        elseif m=="agent.trigger_count" then return tonumber(YDT.ydt_get_trigger_count())or 0
        elseif m=="agent.trigger_name" then return ts(YDT.ydt_get_trigger_name(args[1]))
        elseif m=="agent.list_triggers" then local l,n={},YDT.ydt_get_trigger_count()or 0
            for i=0,n-1 do l[#l+1]={name=ts(YDT.ydt_get_trigger_name(i)),disabled=YDT.ydt_get_trigger_disabled(i)>=0 and YDT.ydt_get_trigger_disabled(i)~=0,event_count=YDT.ydt_get_eca_count(i,0)or 0,condition_count=YDT.ydt_get_eca_count(i,1)or 0,action_count=YDT.ydt_get_eca_count(i,2)or 0} end;return l
        elseif m=="agent.dump_all" then local d,n={},YDT.ydt_get_trigger_count()or 0
            for i=0,n-1 do local e={name=ts(YDT.ydt_get_trigger_name(i)),disabled=YDT.ydt_get_trigger_disabled(i)>=0 and YDT.ydt_get_trigger_disabled(i)~=0}
                e.events,e.conditions,e.actions={},{},{}
                for ei=0,(YDT.ydt_get_eca_count(i,0)or 0)-1 do e.events[#e.events+1]={func=ts(YDT.ydt_get_eca_func_name(i,0,ei))} end
                for ci=0,(YDT.ydt_get_eca_count(i,1)or 0)-1 do e.conditions[#e.conditions+1]={func=ts(YDT.ydt_get_eca_func_name(i,1,ci))} end
                for ai=0,(YDT.ydt_get_eca_count(i,2)or 0)-1 do e.actions[#e.actions+1]={func=ts(YDT.ydt_get_eca_func_name(i,2,ai))} end
                d[#d+1]=e end;return d
        elseif m=="agent.set_trigger_name" then return YDT.ydt_set_trigger_name(args[1],args[2])~=0
        elseif m=="agent.set_trigger_disabled" then return YDT.ydt_set_trigger_disabled(args[1],args[2]and 1 or 0)~=0
        elseif m=="agent.set_eca_param_value" then return YDT.ydt_set_eca_param_value(args[1],args[2],args[3],args[4],args[5])~=0
        elseif m=="agent.add_eca" then return YDT.ydt_add_eca(args[1],args[2])~=0
        elseif m=="agent.remove_eca" then return YDT.ydt_remove_eca(args[1],args[2],args[3])~=0
        elseif m=="agent.object_read" then return ts(YDT.ydt_read_object_file(args[1]))
        elseif m=="agent.object_write" then return YDT.ydt_write_object_file(args[1],args[2])~=0
        end;return nil,-32601,"Method not found: "..m
    end

    local PORT=27118
    local server,err=socket.bind("tcp","127.0.0.1",PORT)
    if not server then f:write("socket.bind failed: "..tostring(err).."\n");f:close();return end
    f:write("TCP bound to "..PORT.."\n");f:close()

    local log = require "log"
    log.info("AgentWorker: listening on 127.0.0.1:"..PORT)

    while true do
        local rd=socket.select({server},nil,1)
        if rd and rd[1] then
            local client=server:accept()
            if client then
                pcall(function()
                    local data=""
                    repeat local r2=socket.select({client},nil,0.2)
                        if r2 and r2[1] then local chunk=client:recv(8192);if chunk and chunk~=false then data=data..chunk end end
                    until not chunk or chunk==nil or #data>1048576
                    if #data>0 then
                        local req=jd(data)
                        if req and req.method then
                            local ok3,r3=pcall(dispatch,req.method,req.params or{})
                            local resp=je({jsonrpc="2.0",id=req.id,result=ok3 and r3 or nil,error=ok3 and nil or{code=-32603,message=tostring(r3)}})
                            local off=1;while off<=#resp do local _,wr=socket.select(nil,{client},1);if not wr or not wr[1] then break end;local n=client:send(resp:sub(off));if not n then break end;off=off+n end
                        end
                    end
                end)
                client:close()
            end
        end
    end
end

-- Entry point: called by module load in thread context
start_server()
