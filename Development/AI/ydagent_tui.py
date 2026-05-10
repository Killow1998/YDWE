#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Callable

from ydagent_client import rpc_call


class CheckFailed(RuntimeError):
    pass


class Tui:
    def __init__(self) -> None:
        self.passed = 0
        self.failed = 0

    def header(self, text: str) -> None:
        print(f"\n=== {text} ===")

    def info(self, text: str) -> None:
        print(f"[INFO] {text}")

    def pass_(self, text: str) -> None:
        self.passed += 1
        print(f"[PASS] {text}")

    def fail(self, text: str) -> None:
        self.failed += 1
        print(f"[FAIL] {text}")

    def summary(self) -> None:
        print(f"\n=== Summary: {self.passed} passed, {self.failed} failed ===")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def posix(path: Path) -> str:
    return path.resolve().as_posix()


def lua_string(value: str) -> str:
    return json.dumps(value)


def build_stub_lua(root: Path, port: int) -> str:
    component = root / "Development" / "Component"
    plugin = component / "plugin"
    bin_dir = component / "bin"
    package_path = f"{posix(plugin)}/?.lua;"
    package_cpath = f"{posix(bin_dir)}/?.dll;"
    component_root = posix(component)
    return f"""
package.path = {lua_string(package_path)} .. package.path
package.cpath = {lua_string(package_cpath)} .. package.cpath
_G.YDAGENT_PORT = {port}
_G.YDAGENT_COMPONENT_ROOT = {lua_string(component_root)}
_G.YDAGENT_TEST_APPROVE_APPLY = true
local triggers = {{
    {{
        name = "TUI_Trigger_0",
        disabled = 0,
        ecas = {{
            [0] = {{ {{ func = "MapInitializationEvent", gui = 100, params = {{}} }} }},
            [1] = {{}},
            [2] = {{ {{ func = "CreateUnit", gui = 200, params = {{ "hfoo", "0", "0" }} }} }},
        }},
    }},
}}
local globals = {{
    {{ name = "udg_TuiInt", type = 3, value = "42" }},
    {{ name = "udg_TuiString", type = 4, value = "hello" }},
}}
local function trig(i) return triggers[(tonumber(i) or -1) + 1] end
local function eca(i, t, e) local tr = trig(i); local list = tr and tr.ecas[tonumber(t) or -1] or nil; return list and list[(tonumber(e) or -1) + 1] or nil end
local function glob(i) return globals[(tonumber(i) or -1) + 1] end
_G.YDAGENT_TEST_STUB = {{
    ydt_refresh = function() return #triggers end,
    ydt_get_trigger_count = function() return #triggers end,
    ydt_get_trigger_name = function(i) local t = trig(i); return t and t.name or nil end,
    ydt_get_trigger_disabled = function(i) local t = trig(i); return t and t.disabled or -1 end,
    ydt_get_eca_count = function(i, typ) local t = trig(i); local list = t and t.ecas[tonumber(typ) or -1] or nil; return list and #list or 0 end,
    ydt_get_eca_func_name = function(i, typ, e) local node = eca(i, typ, e); return node and node.func or nil end,
    ydt_get_eca_gui_id = function(i, typ, e) local node = eca(i, typ, e); return node and node.gui or -1 end,
    ydt_get_eca_param_count = function(i, typ, e) local node = eca(i, typ, e); return node and #node.params or 0 end,
    ydt_get_eca_param_value = function(i, typ, e, p) local node = eca(i, typ, e); return node and node.params[(tonumber(p) or -1) + 1] or nil end,
    ydt_set_trigger_name = function(i, name) local t = trig(i); if not t then return 0 end; t.name = tostring(name or ""); return 1 end,
    ydt_set_trigger_disabled = function(i, disabled) local t = trig(i); if not t then return 0 end; t.disabled = tonumber(disabled) or 0; return 1 end,
    ydt_set_eca_func_name = function(i, typ, e, name) local node = eca(i, typ, e); if not node then return 0 end; node.func = tostring(name or ""); return 1 end,
    ydt_set_eca_active = function() return 1 end,
    ydt_set_eca_param_value = function(i, typ, e, p, value) local node = eca(i, typ, e); if not node then return 0 end; node.params[(tonumber(p) or -1) + 1] = tostring(value or ""); return 1 end,
    ydt_add_eca = function(i, typ) local t = trig(i); if not t then return 0 end; local key = tonumber(typ) or 2; t.ecas[key] = t.ecas[key] or {{}}; t.ecas[key][#t.ecas[key] + 1] = {{ func = "TuiAddedEca", gui = 300, params = {{}} }}; return 1 end,
    ydt_remove_eca = function(i, typ, e) local t = trig(i); local list = t and t.ecas[tonumber(typ) or -1] or nil; local idx = (tonumber(e) or -1) + 1; if not list or not list[idx] then return 0 end; table.remove(list, idx); return 1 end,
    ydt_create_trigger = function(name) triggers[#triggers + 1] = {{ name = tostring(name or "TUI_New_Trigger"), disabled = 0, ecas = {{ [0] = {{}}, [1] = {{}}, [2] = {{}} }} }}; return #triggers end,
    ydt_delete_trigger = function(i) local idx = (tonumber(i) or -1) + 1; if not triggers[idx] then return 0 end; table.remove(triggers, idx); return 1 end,
    ydt_get_global_count = function() return #globals end,
    ydt_get_global_name = function(i) local g = glob(i); return g and g.name or nil end,
    ydt_get_global_type = function(i) local g = glob(i); return g and g.type or -1 end,
    ydt_get_global_value = function(i) local g = glob(i); return g and g.value or nil end,
    ydt_global_diag = function() return '{{"stub":true,"cached_count":2}}' end,
    ydt_read_object_file = function() return nil end,
    ydt_write_object_file = function() return 1 end,
}}
require "YDAgentServerWorker"
"""


def wait_for_rpc(host: str, port: int, timeout: float) -> dict[str, Any]:
    deadline = time.time() + timeout
    last_error = ""
    while time.time() < deadline:
        try:
            result = rpc_call(host, port, "diag.status")
            if isinstance(result, dict) and result.get("ok") is True:
                return result
            last_error = repr(result)
        except Exception as exc:
            last_error = str(exc)
        time.sleep(0.25)
    raise CheckFailed(f"RPC server unavailable on {host}:{port}: {last_error}")


def expect(name: str, fn: Callable[[], Any], tui: Tui) -> Any:
    try:
        result = fn()
        tui.pass_(name)
        return result
    except Exception as exc:
        tui.fail(f"{name}: {exc}")
        raise


def require(condition: bool, message: str) -> None:
    if not condition:
        raise CheckFailed(message)


def run_rpc_suite(host: str, port: int, restore: bool, tui: Tui) -> None:
    status = expect("diag.status", lambda: rpc_call(host, port, "diag.status"), tui)
    require(isinstance(status, dict) and status.get("ok") is True, f"bad status: {status!r}")

    smoke = expect("diag.smoke", lambda: rpc_call(host, port, "diag.smoke"), tui)
    require(isinstance(smoke, dict) and smoke.get("ok") is True, f"bad smoke: {smoke!r}")

    triggers = expect("agent.list_triggers", lambda: rpc_call(host, port, "agent.list_triggers"), tui)
    require(isinstance(triggers, list), "triggers is not a list")

    globals_ = expect("agent.list_globals", lambda: rpc_call(host, port, "agent.list_globals"), tui)
    require(isinstance(globals_, list), "globals is not a list")

    object_types = expect("object.types", lambda: rpc_call(host, port, "object.types"), tui)
    require(isinstance(object_types, list), "object types is not a list")
    object_files = {entry.get("name"): entry.get("file") for entry in object_types if isinstance(entry, dict)}
    require(object_files.get("unit") == "war3map.w3u", f"bad unit mapping: {object_files!r}")
    require(object_files.get("item") == "war3map.w3t", f"bad item mapping: {object_files!r}")
    require(object_files.get("destructable") == "war3map.w3b", f"bad destructable mapping: {object_files!r}")
    require(object_files.get("destructible") == "war3map.w3b", f"bad destructible mapping: {object_files!r}")
    require(object_files.get("doodad") == "war3map.w3d", f"bad doodad mapping: {object_files!r}")
    require(object_files.get("ability") == "war3map.w3a", f"bad ability mapping: {object_files!r}")
    require(object_files.get("buff") == "war3map.w3h", f"bad buff mapping: {object_files!r}")
    require(object_files.get("upgrade") == "war3map.w3q", f"bad upgrade mapping: {object_files!r}")

    pending = expect("agent.list_pending_globals", lambda: rpc_call(host, port, "agent.list_pending_globals", ["*"]), tui)
    require(isinstance(pending, dict), "pending globals is not an object")

    cleared_fake = expect(
        "agent.clear_pending_globals fake map",
        lambda: rpc_call(host, port, "agent.clear_pending_globals", ["Q:/ydagent_tui_fake_map.w3x", "udg_TuiInt"]),
        tui,
    )
    require(cleared_fake is True, f"clear fake pending global returned {cleared_fake!r}")

    context = expect("agent.compress_context", lambda: rpc_call(host, port, "agent.compress_context", [{"trigger_limit": 3, "node_limit": 3}]), tui)
    require(isinstance(context, dict), "compressed context is not an object")

    for eca_type, label in [(0, "event"), (1, "condition"), (2, "action")]:
        before_count = expect(
            f"agent.eca_count {label}",
            lambda eca_type=eca_type: rpc_call(host, port, "agent.eca_count", [0, eca_type]),
            tui,
        )
        require(isinstance(before_count, int), f"{label} eca count is not int: {before_count!r}")
        added = expect(
            f"agent.add_eca {label}",
            lambda eca_type=eca_type: rpc_call(host, port, "agent.add_eca", [0, eca_type]),
            tui,
        )
        require(added is True, f"add_eca {label} failed: {added!r}")
        new_index = before_count
        after_add = expect(
            f"agent.eca_count {label} after add",
            lambda eca_type=eca_type: rpc_call(host, port, "agent.eca_count", [0, eca_type]),
            tui,
        )
        require(after_add == before_count + 1, f"add_eca {label} count mismatch: {after_add!r}")
        func_name = f"Tui{label.title()}Func"
        set_func = expect(
            f"agent.set_eca_func_name {label}",
            lambda eca_type=eca_type, new_index=new_index, func_name=func_name: rpc_call(
                host, port, "agent.set_eca_func_name", [0, eca_type, new_index, func_name]
            ),
            tui,
        )
        require(set_func is True, f"set_eca_func_name {label} failed: {set_func!r}")
        got_func = expect(
            f"agent.eca_func_name {label}",
            lambda eca_type=eca_type, new_index=new_index: rpc_call(host, port, "agent.eca_func_name", [0, eca_type, new_index]),
            tui,
        )
        require(got_func == func_name, f"eca func {label} mismatch: {got_func!r}")
        param_value = f"{label}_param"
        set_param = expect(
            f"agent.set_eca_param_value {label}",
            lambda eca_type=eca_type, new_index=new_index, param_value=param_value: rpc_call(
                host, port, "agent.set_eca_param_value", [0, eca_type, new_index, 0, param_value]
            ),
            tui,
        )
        require(set_param is True, f"set_eca_param_value {label} failed: {set_param!r}")
        got_param = expect(
            f"agent.eca_param_value {label}",
            lambda eca_type=eca_type, new_index=new_index: rpc_call(host, port, "agent.eca_param_value", [0, eca_type, new_index, 0]),
            tui,
        )
        require(got_param == param_value, f"eca param {label} mismatch: {got_param!r}")
        removed = expect(
            f"agent.remove_eca {label}",
            lambda eca_type=eca_type, new_index=new_index: rpc_call(host, port, "agent.remove_eca", [0, eca_type, new_index]),
            tui,
        )
        require(removed is True, f"remove_eca {label} failed: {removed!r}")
        after_remove = expect(
            f"agent.eca_count {label} after remove",
            lambda eca_type=eca_type: rpc_call(host, port, "agent.eca_count", [0, eca_type]),
            tui,
        )
        require(after_remove == before_count, f"remove_eca {label} count mismatch: {after_remove!r}")

    schema = expect("ai.operation_schema", lambda: rpc_call(host, port, "ai.operation_schema"), tui)
    require(isinstance(schema, dict), "operation schema is not an object")

    dry_run = expect(
        "ai.apply_plan dry-run",
        lambda: rpc_call(host, port, "ai.apply_plan", [{"operations": [{"op": "set_trigger_disabled", "trigger_index": 0, "disabled": False}]}, {"dry_run": True}]),
        tui,
    )
    require(isinstance(dry_run, dict), "dry-run result is not an object")
    require(isinstance(dry_run.get("preview"), list) and len(dry_run["preview"]) == 1, "dry-run preview is missing")
    preview = dry_run["preview"][0]
    snapshot = preview.get("snapshot") if isinstance(preview, dict) else None
    require(isinstance(snapshot, dict), "dry-run preview snapshot is missing")
    require(snapshot.get("target") == "trigger", f"unexpected preview target: {snapshot!r}")
    require(snapshot.get("field") == "disabled", f"unexpected preview field: {snapshot!r}")
    require(snapshot.get("after") is False, f"unexpected preview after value: {snapshot!r}")

    object_validation = expect(
        "ai.validate_plan object types",
        lambda: rpc_call(
            host,
            port,
            "ai.validate_plan",
            [
                {
                    "operations": [
                        {
                            "op": "object_set_field",
                            "type_name": "destructable",
                            "record_kind": "custom",
                            "object_id": "B000",
                            "field_id": "unam",
                            "value": "x",
                        }
                    ]
                }
            ],
        ),
        tui,
    )
    require(isinstance(object_validation, dict) and object_validation.get("ok") is True, f"bad object validation: {object_validation!r}")

    rollback = expect(
        "ai.apply_plan rollback",
        lambda: rpc_call(
            host,
            port,
            "ai.apply_plan",
            [
                {
                    "operations": [
                        {"op": "set_trigger_disabled", "trigger_index": 0, "disabled": True},
                        {"op": "set_trigger_name", "trigger_index": 999, "name": "MissingTrigger"},
                    ]
                },
                {"dry_run": False, "confirm": True},
            ],
        ),
        tui,
    )
    require(isinstance(rollback, dict), "rollback result is not an object")
    require(rollback.get("ok") is False, f"rollback scenario should fail: {rollback!r}")
    require(isinstance(rollback.get("rollback_results"), list), f"rollback results missing: {rollback!r}")
    require(len(rollback["rollback_results"]) == 1, f"unexpected rollback count: {rollback!r}")
    require(rollback["rollback_results"][0].get("ok") is True, f"rollback failed: {rollback!r}")
    disabled_after_rollback = expect(
        "agent.trigger_disabled after rollback",
        lambda: rpc_call(host, port, "agent.trigger_disabled", [0]),
        tui,
    )
    require(disabled_after_rollback is False, f"trigger disabled was not restored: {disabled_after_rollback!r}")

    if globals_:
        global_write = expect("agent.set_global_value rejects unsafe write", lambda: rpc_call(host, port, "agent.set_global_value", [0, "99"]), tui)
        require(global_write is False, f"global write should be false, got {global_write!r}")

        global_index = expect("agent.global_index by short name", lambda: rpc_call(host, port, "agent.global_index", ["TuiInt"]), tui)
        require(global_index == 0, f"global index mismatch: {global_index!r}")

        global_info = expect("agent.global_info resolves udg alias", lambda: rpc_call(host, port, "agent.global_info", ["udg_TuiString"]), tui)
        require(isinstance(global_info, dict), "global info is not an object")
        require(global_info.get("index") == 1, f"unexpected global index: {global_info.get('index')!r}")
        require(global_info.get("name") == "udg_TuiString", f"unexpected global name: {global_info.get('name')!r}")

        global_by_name = expect(
            "agent.set_global_value_by_name rejects unsafe write",
            lambda: rpc_call(host, port, "agent.set_global_value_by_name", ["TuiInt", "99"]),
            tui,
        )
        require(global_by_name is False, f"global write by name should be false, got {global_by_name!r}")

    if restore:
        require(len(triggers) > 0, "restore requires at least one trigger")
        original = rpc_call(host, port, "agent.trigger_name", [0])
        require(isinstance(original, str) and original, "invalid original trigger name")
        mutated = f"{original[:200]}__TUI_RESTORE__"
        try:
            ok = expect("agent.set_trigger_name mutate", lambda: rpc_call(host, port, "agent.set_trigger_name", [0, mutated]), tui)
            require(ok is True, f"set_trigger_name returned {ok!r}")
            after = expect("agent.trigger_name verify mutation", lambda: rpc_call(host, port, "agent.trigger_name", [0]), tui)
            require(after == mutated, f"trigger rename mismatch: {after!r}")
        finally:
            rpc_call(host, port, "agent.set_trigger_name", [0, original])
        restored = expect("agent.trigger_name verify restore", lambda: rpc_call(host, port, "agent.trigger_name", [0]), tui)
        require(restored == original, f"trigger restore mismatch: {restored!r}")


class StubWorker:
    def __init__(self, root: Path, port: int) -> None:
        self.root = root
        self.port = port
        self.proc: subprocess.Popen[str] | None = None
        self.script_path: Path | None = None

    def start(self) -> None:
        lua = self.root / "Development" / "Component" / "bin" / "lua.exe"
        if not lua.exists():
            raise CheckFailed(f"lua.exe not found: {lua}")
        handle = tempfile.NamedTemporaryFile("w", suffix="_ydagent_tui_stub.lua", delete=False, encoding="utf-8")
        with handle:
            handle.write(build_stub_lua(self.root, self.port))
        self.script_path = Path(handle.name)
        self.proc = subprocess.Popen(
            [str(lua), str(self.script_path)],
            cwd=str(self.root),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )

    def stop(self) -> None:
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        if self.script_path:
            try:
                self.script_path.unlink()
            except OSError:
                pass

    def output(self) -> str:
        if not self.proc or not self.proc.stdout:
            return ""
        try:
            return self.proc.stdout.read() or ""
        except Exception:
            return ""


def run_stub(args: argparse.Namespace, tui: Tui) -> int:
    root = repo_root()
    worker = StubWorker(root, args.port)
    tui.header("YDWE Agent TUI - stub loopback")
    tui.info(f"repo={root}")
    tui.info(f"port={args.port}")
    try:
        worker.start()
        wait_for_rpc(args.host, args.port, args.wait)
        run_rpc_suite(args.host, args.port, args.restore, tui)
        return 0
    except Exception as exc:
        tui.fail(str(exc))
        return 1
    finally:
        worker.stop()
        out = worker.output().strip()
        if out:
            tui.header("worker output")
            print(out)


def run_live(args: argparse.Namespace, tui: Tui) -> int:
    root = repo_root()
    component = root / "Development" / "Component"
    ydwexe = component / "YDWE.exe"
    proc: subprocess.Popen[Any] | None = None
    tui.header("YDWE Agent TUI - live RPC")
    try:
        if args.start:
            if not ydwexe.exists():
                raise CheckFailed(f"YDWE.exe not found: {ydwexe}")
            cmd = [str(ydwexe)]
            if args.map:
                cmd.extend(["-loadfile", str(Path(args.map).resolve())])
            tui.info("starting " + " ".join(cmd))
            proc = subprocess.Popen(cmd, cwd=str(component))
        wait_for_rpc(args.host, args.port, args.wait)
        run_rpc_suite(args.host, args.port, args.restore, tui)
        return 0
    except Exception as exc:
        tui.fail(str(exc))
        return 1
    finally:
        if proc and args.stop and proc.poll() is None:
            proc.terminate()


def main() -> int:
    parser = argparse.ArgumentParser(description="YDWE Agent self-test TUI/CLI")
    sub = parser.add_subparsers(dest="mode", required=True)

    stub = sub.add_parser("stub", help="run a full JSON-RPC loopback test without YDWE GUI")
    stub.add_argument("--host", default="127.0.0.1")
    stub.add_argument("--port", type=int, default=27119)
    stub.add_argument("--wait", type=float, default=10.0)
    stub.add_argument("--restore", action="store_true", help="verify reversible trigger rename against the stub")

    live = sub.add_parser("live", help="run the same RPC suite against a live YDWE session")
    live.add_argument("--host", default="127.0.0.1")
    live.add_argument("--port", type=int, default=27118)
    live.add_argument("--wait", type=float, default=30.0)
    live.add_argument("--restore", action="store_true", help="verify reversible trigger rename against live YDWE")
    live.add_argument("--start", action="store_true", help="start Development/Component/YDWE.exe first")
    live.add_argument("--stop", action="store_true", help="terminate the YDWE.exe process started by this command")
    live.add_argument("--map", help="map path passed as -loadfile when --start is used")

    args = parser.parse_args()
    tui = Tui()
    code = run_stub(args, tui) if args.mode == "stub" else run_live(args, tui)
    tui.summary()
    return code


if __name__ == "__main__":
    raise SystemExit(main())
