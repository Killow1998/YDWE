local log = require "log"
local ffi = require "ffi"

ffi.cdef([[
    int ydt_get_trigger_count(void); const char* ydt_get_trigger_name(int);
    int ydt_get_trigger_disabled(int); int ydt_get_eca_count(int,int);
    const char* ydt_get_eca_func_name(int,int,int); int ydt_get_eca_gui_id(int,int,int);
    int ydt_get_eca_param_count(int,int,int);
    const char* ydt_get_eca_param_value(int,int,int,int);
    int ydt_set_trigger_name(int,const char*); int ydt_set_trigger_disabled(int,int);
    int ydt_set_eca_func_name(int,int,int,const char*); int ydt_set_eca_active(int,int,int,int);
    int ydt_set_eca_param_value(int,int,int,int,const char*);
    int ydt_add_eca(int,int); int ydt_remove_eca(int,int,int);
    int ydt_create_trigger(const char*);
    int ydt_delete_trigger(int);
]])

local function ts(p)
    if p==nil then return nil end; local s=ffi.string(p); return s=="" and nil or s
end

local function write_report(path, lines)
    local f = (io.__open or io.open)(path, "w")
    if f then
        for _,l in ipairs(lines) do f:write(l.."\n") end
        f:close()
    end
end

local RESULTS = {}

local function ok(name, cond)
    RESULTS[#RESULTS+1] = string.format("[%s] %s", cond and "PASS" or "FAIL", name)
    if not cond then log.error("TEST FAIL: " .. name) end
    return cond
end

local function run_all_tests(dll)
    RESULTS = {}
    local tc = dll.ydt_get_trigger_count()
    ok("trigger_count", tc > 0)

    if tc == 0 then
        RESULTS[#RESULTS+1] = "ABORT: no triggers captured"
        return
    end

    local name0 = ts(dll.ydt_get_trigger_name(0)) or "?"

    -- 1. list_triggers
    local list = {}
    for i=0,tc-1 do list[#list+1]={i=i,name=ts(dll.ydt_get_trigger_name(i))} end
    ok("list_triggers("..tc.." triggers)", #list == tc)
    RESULTS[#RESULTS+1] = "  Trigger[0]="..(list[1] and list[1].name or "?")

    -- 2. rename trigger
    local saved_name = ts(dll.ydt_get_trigger_name(0))
    local rename_ok = dll.ydt_set_trigger_name(0, "AI_TestRanamed") ~= 0
    ok("set_trigger_name", rename_ok)
    -- revert
    dll.ydt_set_trigger_name(0, saved_name)

    -- 3. disable/enable trigger
    local was_disabled = dll.ydt_get_trigger_disabled(0) ~= 0
    dll.ydt_set_trigger_disabled(0, 1)
    local is_disabled = dll.ydt_get_trigger_disabled(0) ~= 0
    ok("set_trigger_disabled(on)", is_disabled)
    dll.ydt_set_trigger_disabled(0, 0)
    local is_enabled = dll.ydt_get_trigger_disabled(0) == 0
    ok("set_trigger_disabled(off)", is_enabled)

    -- 4. ECA counts
    local ec = dll.ydt_get_eca_count(0,0) or 0
    local cc = dll.ydt_get_eca_count(0,1) or 0
    local ac = dll.ydt_get_eca_count(0,2) or 0
    RESULTS[#RESULTS+1] = string.format("  Trigger[0] E:%d C:%d A:%d", ec, cc, ac)

    -- 5. ECA function name
    if ac > 0 then
        local fn = ts(dll.ydt_get_eca_func_name(0,2,0)) or "?"
        local gid = dll.ydt_get_eca_gui_id(0,2,0)
        RESULTS[#RESULTS+1] = string.format("  Action[0]=%s gui:%d", fn, gid or -1)

        -- 6. ECA params
        local pc = dll.ydt_get_eca_param_count(0,2,0) or 0
        ok("eca_param_count", pc >= 0)
        local params = {}
        for p=0,pc-1 do params[p+1]=ts(dll.ydt_get_eca_param_value(0,2,0,p)) or "(nil)" end
        RESULTS[#RESULTS+1] = "  Params: " .. table.concat(params, ", ")

        -- 7. set param value
        if pc > 0 then
            local saved = ts(dll.ydt_get_eca_param_value(0,2,0,0)) or ""
            dll.ydt_set_eca_param_value(0,2,0,0, "AI_TEST_VALUE")
            local new_val = ts(dll.ydt_get_eca_param_value(0,2,0,0)) or ""
            ok("set_eca_param_value", new_val == "AI_TEST_VALUE")
            dll.ydt_set_eca_param_value(0,2,0,0, saved) -- revert
        end
    end

    -- 8. add_eca (creates CommentString)
    local old_ac = dll.ydt_get_eca_count(0,2) or 0
    local add_ok = dll.ydt_add_eca(0,2) ~= 0
    local new_ac = dll.ydt_get_eca_count(0,2) or 0
    ok("add_eca", add_ok and new_ac == old_ac + 1)
    RESULTS[#RESULTS+1] = string.format("  Actions: %d -> %d", old_ac, new_ac)

    -- 9. set func name on new action
    if new_ac > old_ac then
        local new_idx = new_ac - 1
        local old_fn = ts(dll.ydt_get_eca_func_name(0,2,new_idx)) or ""
        dll.ydt_set_eca_func_name(0,2,new_idx, "DoNothing")
        local new_fn = ts(dll.ydt_get_eca_func_name(0,2,new_idx)) or ""
        ok("set_eca_func_name", new_fn == "DoNothing")
        dll.ydt_set_eca_func_name(0,2,new_idx, old_fn) -- revert
    end

    -- 10. set_eca_active (toggle off/on)
    if new_ac > old_ac then
        local idx = new_ac - 1
        dll.ydt_set_eca_active(0,2,idx, 0)
        local count_after_off = 0
        for i=0,new_ac-1 do
            -- count_eca only counts active nodes
        end
        -- verify via re-read
        local after_off = dll.ydt_get_eca_count(0,2) or 0
        ok("set_eca_active(off)", after_off == old_ac) -- should be back to original
        dll.ydt_set_eca_active(0,2,idx, 1)
        local after_on = dll.ydt_get_eca_count(0,2) or 0
        ok("set_eca_active(on)", after_on == new_ac)
    end

    -- 11. remove_eca
    if new_ac > old_ac then
        local rem_ok = dll.ydt_remove_eca(0,2,new_ac-1) ~= 0
        local after_rem = dll.ydt_get_eca_count(0,2) or 0
        ok("remove_eca", rem_ok and after_rem == old_ac)
        RESULTS[#RESULTS+1] = string.format("  Actions: %d -> %d", new_ac, after_rem)
    end

    -- 12. create_trigger
    local old_tc = dll.ydt_get_trigger_count()
    dll.ydt_create_trigger("AI_CreatedTrigger")
    local new_tc = dll.ydt_get_trigger_count()
    ok("create_trigger", new_tc == old_tc + 1)
    RESULTS[#RESULTS+1] = string.format("  Triggers: %d -> %d", old_tc, new_tc)

    -- 13. verify new trigger is accessible by name
    if new_tc > old_tc then
        local new_idx = new_tc - 1
        local new_name = ts(dll.ydt_get_trigger_name(new_idx)) or ""
        ok("new trigger name matches", new_name == "AI_CreatedTrigger")
        RESULTS[#RESULTS+1] = string.format("  New trigger[%d] = '%s'", new_idx, new_name)

        -- 14. delete_trigger
        local del_ok = dll.ydt_delete_trigger(new_idx) ~= 0
        local after_del = dll.ydt_get_trigger_count()
        ok("delete_trigger", del_ok and after_del == old_tc)
        RESULTS[#RESULTS+1] = string.format("  After delete: %d -> %d triggers", new_tc, after_del)
    end

    -- Write report
    local report_path = "Q:/AppData/ydwe/YDWE/Development/Component/logs/test_report.txt"
    write_report(report_path, RESULTS)
    log.info("AgentDump: FULL TEST COMPLETE. Results in logs/test_report.txt")
    local pass = 0; local fail = 0
    for _,l in ipairs(RESULTS) do
        if l:find("[PASS]") then pass=pass+1 end
        if l:find("[FAIL]") then fail=fail+1 end
    end
    log.info(string.format("AgentDump: %d PASS, %d FAIL", pass, fail))
end

local loader = {}
loader.load = function(path)
    log.info("AgentDump: AutoTest mode. Save map to run all tests.")
    local dll = ffi.load("YDTrigger.dll")
    if not dll then log.error("AgentDump: no DLL"); return false end

    local ok, ev = pcall(require, "ev")
    if ok and ev then
        ev.on('编译地图', function(success)
            if not success then return end
            run_all_tests(dll)
        end)
        log.info("AgentDump: test hook registered")
    end
    return true
end
loader.unload = function() end
return loader
