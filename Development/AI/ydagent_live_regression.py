#!/usr/bin/env python3
"""YDWE Agent live regression harness."""

from __future__ import annotations

import argparse
import subprocess
import time
from pathlib import Path
from typing import Any

from ydagent_client import (
    is_lni_marker_path,
    normalize_live_value,
    parse_json_arg,
    rpc_call,
    wait_for_server,
)


class RegressionError(RuntimeError):
    pass


_DEFAULT_YDWE_EXE = Path(r"Q:\AppData\ydwe\YDWE\Build\publish\Debug\YDWE.exe")
_DEFAULT_MAP = Path(r"Q:\AppData\ydwe\work\compose_demo_gui_only_v2.w3x")
_DEFAULT_HOST = "127.0.0.1"
_DEFAULT_PORT = 27118
_DEFAULT_GLOBAL_NAME = "udg_compose_count"
_DEFAULT_GLOBAL_VALUE = "17"


def _assert(cond: bool, message: str) -> None:
    if not cond:
        raise RegressionError(message)


def _normalize_path(path: str | Path | None) -> str:
    if path is None:
        return ""
    value = str(path).strip().strip('"').strip("'")
    if not value:
        return ""
    try:
        return str(Path(value).resolve()).replace("/", "\\").lower()
    except Exception:
        return value.replace("/", "\\").strip().lower()


def _launch_ydwe(exe_path: Path, map_path: Path | None) -> subprocess.Popen[Any]:
    if not exe_path.exists():
        raise RegressionError(f"YDWE.exe not found: {exe_path}")
    cmd: list[str] = [str(exe_path)]
    if map_path:
        cmd.extend(["-loadfile", str(map_path)])
    proc = subprocess.Popen(
        cmd,
        cwd=str(exe_path.parent),
        creationflags=subprocess.CREATE_NEW_CONSOLE,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return proc


def _agent_is_available(host: str, port: int, timeout: float = 1.0) -> bool:
    try:
        status = _rpc(host, port, "diag.status", timeout=timeout)
        return isinstance(status, dict) and status.get("ok") is True
    except Exception:
        return False


def _close_launched_process(proc: subprocess.Popen[Any]) -> None:
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=3.0)
    except subprocess.TimeoutExpired:
        proc.kill()


def _rpc(
    host: str,
    port: int,
    method: str,
    params: list[Any] | None = None,
    timeout: float = 10.0,
) -> Any:
    return rpc_call(host, port, method, params or [], timeout=timeout)


def _wait_for_editor_server(host: str, port: int, timeout: float) -> None:
    wait_for_server(host, port, timeout=timeout)


def _read_current_map_path(host: str, port: int) -> str:
    value = _rpc(host, port, "editor.current_map_path")
    _assert(isinstance(value, str), "editor.current_map_path returned non-string")
    return value


def _run_save_map(host: str, port: int, timeout: float) -> None:
    result = _rpc(host, port, "editor.save_map", timeout=timeout)
    _assert(isinstance(result, dict), f"editor.save_map returned unexpected value: {result!r}")
    _wait_for_editor_server(host, port, 30.0)


def _global_scalar_cycle(
    host: str,
    port: int,
    global_name: str,
    global_value: Any,
    rpc_timeout: float,
    save_timeout: float,
) -> None:
    info = _rpc(host, port, "agent.global_info", [global_name], timeout=rpc_timeout)
    _assert(isinstance(info, dict), "agent.global_info returned non-dict")
    _assert(isinstance(info.get("index"), int), "global_info missing integer index")
    _assert(not info.get("array"), "refusing non-scalar global")

    original_value = info.get("value")
    index = info["index"]
    map_path = _read_current_map_path(host, port)

    mutated = False
    try:
        write_ok = _rpc(
            host,
            port,
            "agent.set_global_value_by_name",
            [global_name, global_value],
            timeout=rpc_timeout,
        )
        _assert(write_ok is True, "agent.set_global_value_by_name returned False")
        mutated = True

        if not is_lni_marker_path(map_path):
            _run_save_map(host, port, save_timeout)

        value_after = _rpc(host, port, "agent.global_value", [index], timeout=rpc_timeout)
        _assert(
            normalize_live_value(value_after) == normalize_live_value(global_value),
            f"global readback mismatch, expected {global_value!r}, got {value_after!r}",
        )
    finally:
        if mutated:
            _run_restore(host, port, global_name, original_value, rpc_timeout)
            _run_save_map(host, port, save_timeout)
            restored_value = _rpc(host, port, "agent.global_value", [index], timeout=rpc_timeout)
            _assert(
                normalize_live_value(restored_value) == normalize_live_value(original_value),
                f"global restore mismatch, expected {original_value!r}, got {restored_value!r}",
            )


def _run_restore(
    host: str,
    port: int,
    global_name: str,
    value: Any,
    rpc_timeout: float,
) -> None:
    restore_ok = _rpc(
        host,
        port,
        "agent.set_global_value_by_name",
        [global_name, value],
        timeout=rpc_timeout,
    )
    _assert(restore_ok is True, "restore write returned False")


def run(args: argparse.Namespace) -> subprocess.Popen[Any] | None:
    map_path = args.map_path
    target_map = _normalize_path(map_path)
    proc: subprocess.Popen[Any] | None = None

    if args.no_launch and args.close_launched:
        print("INFO: --close-launched ignored when --no-launch is set")

    if not args.no_launch:
        if _agent_is_available(args.host, args.port):
            raise RegressionError(
                "Agent server is already running; use --no-launch for the current "
                "session or close the existing YDWE session before launching"
            )
        proc = _launch_ydwe(args.ydwe_exe, map_path)
        print(f"PASS: launched_pid={proc.pid}")
        time.sleep(0.5)

    _wait_for_editor_server(args.host, args.port, args.wait)
    print(f"PASS: server_ready host={args.host} port={args.port}")

    current_map = _read_current_map_path(args.host, args.port)
    _assert(
        _normalize_path(current_map) == target_map,
        f"map mismatch (requested={map_path}, current={current_map})",
    )
    print("PASS: map_path_verified")

    _run_save_map(args.host, args.port, args.save_timeout)
    print("PASS: editor.save_map_ok")

    _global_scalar_cycle(
        args.host,
        args.port,
        args.global_name,
        args.global_value,
        args.rpc_timeout,
        args.save_timeout,
    )
    print(
        f"PASS: global_scalar_restore name={args.global_name} "
        f"target={args.global_value!r}"
    )
    return proc


def main() -> int:
    parser = argparse.ArgumentParser(description="YDWE Agent live regression harness")
    parser.add_argument(
        "--ydwe-exe",
        type=Path,
        default=_DEFAULT_YDWE_EXE,
        help=f"YDWE executable path (default: {_DEFAULT_YDWE_EXE})",
    )
    parser.add_argument(
        "--map",
        dest="map_path",
        type=Path,
        default=_DEFAULT_MAP,
        help=f"map path to open (default: {_DEFAULT_MAP})",
    )
    parser.add_argument(
        "--global-name",
        default=_DEFAULT_GLOBAL_NAME,
        help=f"global variable name to check (default: {_DEFAULT_GLOBAL_NAME})",
    )
    parser.add_argument(
        "--global-value",
        default=_DEFAULT_GLOBAL_VALUE,
        help=f"global value to set as JSON value (default: {_DEFAULT_GLOBAL_VALUE})",
    )
    parser.add_argument(
        "--host",
        default=_DEFAULT_HOST,
        help=f"Agent host (default: {_DEFAULT_HOST})",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=_DEFAULT_PORT,
        help=f"Agent port (default: {_DEFAULT_PORT})",
    )
    parser.add_argument(
        "--wait",
        type=float,
        default=30.0,
        help="seconds to wait for Agent server availability (default: 30)",
    )
    parser.add_argument(
        "--save-timeout",
        type=float,
        default=60.0,
        help="timeout for editor.save_map RPC (default: 60)",
    )
    parser.add_argument(
        "--rpc-timeout",
        type=float,
        default=10.0,
        help="timeout for non-save Agent RPC calls (default: 10)",
    )
    parser.add_argument(
        "--no-launch",
        action="store_true",
        help="do not spawn YDWE.exe; validate an already-running session",
    )
    parser.add_argument(
        "--close-launched",
        action="store_true",
        help="terminate only the process launched by this script at shutdown",
    )
    args = parser.parse_args()

    args.global_value = parse_json_arg(args.global_value)
    launched_proc: subprocess.Popen[Any] | None = None

    try:
        launched_proc = run(args)
        print("PASS: regression_complete")
        return 0
    except Exception as exc:  # noqa: BLE001
        print(f"FAIL: {exc}")
        return 1
    finally:
        if args.close_launched and launched_proc is not None:
            _close_launched_process(launched_proc)


if __name__ == "__main__":
    raise SystemExit(main())
