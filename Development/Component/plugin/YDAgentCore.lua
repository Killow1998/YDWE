-- YDWE AI Agent Core Plugin - Compatible with YDWE 1.32.13+
local ffi = require "ffi"
local log = require "log"

local loader = {}
local YDT = nil

loader.load = function(path)
    ffi.cdef([[
        int  ydt_refresh(void); int  ydt_get_trigger_count(void);
        const char* ydt_get_trigger_name(int); int  ydt_get_trigger_disabled(int);
        int  ydt_get_eca_count(int,int); const char* ydt_get_eca_func_name(int,int,int);
        int  ydt_get_eca_gui_id(int,int,int); int  ydt_get_eca_param_count(int,int,int);
        const char* ydt_get_eca_param_value(int,int,int,int);
        int  ydt_set_trigger_name(int,const char*); int  ydt_set_trigger_disabled(int,int);
        int  ydt_set_eca_func_name(int,int,int,const char*); int  ydt_set_eca_active(int,int,int,int);
        int  ydt_set_eca_param_value(int,int,int,int,const char*);
        int  ydt_add_eca(int,int); int  ydt_remove_eca(int,int,int);
        const char* ydt_read_object_file(const char*); int  ydt_write_object_file(const char*,const char*);
    ]])
    local ok, dll = pcall(ffi.load, path:string())
    if not ok then log.error('YDAgentCore: DLL load failed: '..tostring(dll)); return false end
    YDT = dll
    log.info('YDAgentCore: YDTrigger.dll loaded (18 exports)')
    return true
end

loader.unload = function() YDT = nil end

local agent = {}; agent.EVENT=0; agent.CONDITION=1; agent.ACTION=2
local function ts(p) if p==nil then return nil end; local s=ffi.string(p); return s=="" and nil or s end
function agent.refresh()             local n=YDT.ydt_refresh(); return n>0 and n or nil end
function agent.trigger_count()        return YDT and tonumber(YDT.ydt_get_trigger_count())or 0 end
function agent.trigger_name(i)        return YDT and ts(YDT.ydt_get_trigger_name(i)) end
function agent.trigger_disabled(i)    local r=YDT.ydt_get_trigger_disabled(i); return r>=0 and(r~=0)or nil end
function agent.eca_count(i,t)         return YDT and tonumber(YDT.ydt_get_eca_count(i,t))or 0 end
function agent.eca_func_name(i,t,ei)  return YDT and ts(YDT.ydt_get_eca_func_name(i,t,ei)) end
function agent.eca_gui_id(i,t,ei)     local r=YDT.ydt_get_eca_gui_id(i,t,ei); return r>=0 and r or nil end
function agent.eca_param_count(i,t,ei) return YDT and tonumber(YDT.ydt_get_eca_param_count(i,t,ei))or 0 end
function agent.eca_param_value(i,t,ei,p) return YDT and ts(YDT.ydt_get_eca_param_value(i,t,ei,p)) end
function agent.set_trigger_name(i,n)     return YDT and YDT.ydt_set_trigger_name(i,n)~=0 or false end
function agent.set_trigger_disabled(i,d) return YDT and YDT.ydt_set_trigger_disabled(i,d and 1 or 0)~=0 or false end
function agent.set_eca_func_name(i,t,ei,n) return YDT and YDT.ydt_set_eca_func_name(i,t,ei,n)~=0 or false end
function agent.set_eca_active(i,t,ei,a)   return YDT and YDT.ydt_set_eca_active(i,t,ei,a and 1 or 0)~=0 or false end
function agent.set_eca_param_value(i,t,ei,p,v) return YDT and YDT.ydt_set_eca_param_value(i,t,ei,p,v)~=0 or false end
function agent.add_eca(i,t)               return YDT and YDT.ydt_add_eca(i,t)~=0 or false end
function agent.remove_eca(i,t,ei)         return YDT and YDT.ydt_remove_eca(i,t,ei)~=0 or false end
local function rel(i,t) local n=agent.eca_count(i,t); if n==0 then return{}end; local l={}
    for j=0,n-1 do local fn=agent.eca_func_name(i,t,j)
        if fn then local nd={func=fn,gui_id=agent.eca_gui_id(i,t,j),params={}}
            for p=0,(agent.eca_param_count(i,t,j)or 0)-1 do nd.params[p+1]=agent.eca_param_value(i,t,j,p) end
            l[#l+1]=nd end end; return l end
function agent.get_eca_tree(i) if i<0 or i>=agent.trigger_count()then return nil end
    return{events=rel(i,0),conditions=rel(i,1),actions=rel(i,2)} end
function agent.list_triggers() local l,n={},agent.trigger_count()
    for i=0,n-1 do l[#l+1]={name=agent.trigger_name(i),disabled=agent.trigger_disabled(i),
        event_count=agent.eca_count(i,0),condition_count=agent.eca_count(i,1),action_count=agent.eca_count(i,2)} end
    return l end
function agent.dump_all() local d,n={},agent.trigger_count()
    for i=0,n-1 do local e={name=agent.trigger_name(i),disabled=agent.trigger_disabled(i)}
        local t=agent.get_eca_tree(i); if t then e.events=t.events;e.conditions=t.conditions;e.actions=t.actions end
        d[#d+1]=e end; return d end

_G.ydwe_agent = agent
return loader
