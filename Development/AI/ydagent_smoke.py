#!/usr/bin/env python3
"""YDWE Agent runtime smoke test.

Examples:
    python Development\AI\ydagent_smoke.py
    python Development\AI\ydagent_smoke.py --restore
"""

from __future__ import annotations

import argparse
import datetime as dt
import time
from typing import Any

from ydagent_client import (
    is_lni_marker_path,
    normalize_live_value,
    parse_json_arg,
    rpc_call,
)

_RPC_TIMEOUT_DEFAULT = 10.0
_RPC_TIMEOUT_SLOW = 60.0

class SmokeError(RuntimeError):
    pass


def _rpc(
    host: str,
    port: int,
    method: str,
    params: list[Any] | None = None,
    timeout: float = _RPC_TIMEOUT_DEFAULT,
) -> Any:
    return rpc_call(host, port, method, params or [], timeout=timeout)


def _wait_for_server(host: str, port: int, wait_seconds: float, interval_seconds: float) -> dict[str, Any]:
    deadline = time.time() + wait_seconds
    last_error = ""
    while time.time() < deadline:
        try:
            status = _rpc(host, port, "diag.status")
            if isinstance(status, dict) and status.get("ok"):
                return status
            last_error = f"diag.status returned: {status!r}"
        except Exception as exc:
            last_error = str(exc)
        time.sleep(interval_seconds)
    raise SmokeError(f"TCP unavailable after {wait_seconds:.1f}s: {last_error}")


def _assert(cond: bool, message: str) -> None:
    if not cond:
        raise SmokeError(message)


def _build_trigger_name(original: Any, suffix: str) -> str:
    marker = f"__AI_SMOKE_{suffix}"
    base = original if isinstance(original, str) and original else "AI_Smoke_Trigger"
    max_len = 255
    if len(marker) >= max_len:
        return marker[:max_len]
    keep = max_len - len(marker)
    return f"{base[:keep]}{marker}"


def _run_restore_cycle(
    host: str,
    port: int,
    trigger_index: int,
) -> dict[str, Any]:
    original_trigger_name = _rpc(host, port, "agent.trigger_name", [trigger_index])
    _assert(isinstance(original_trigger_name, str) and original_trigger_name != "", "invalid trigger name snapshot")

    suffix = dt.datetime.now().strftime("%Y%m%d%H%M%S")
    new_trigger_name = _build_trigger_name(original_trigger_name, suffix)

    trigger_touched = False
    try:
        trigger_ok = _rpc(host, port, "agent.set_trigger_name", [trigger_index, new_trigger_name])
        _assert(trigger_ok is True, "agent.set_trigger_name returned False")
        trigger_touched = True
        trigger_after = _rpc(host, port, "agent.trigger_name", [trigger_index])
        _assert(trigger_after == new_trigger_name, "trigger rename verification failed")
    finally:
        restore_errors: list[str] = []
        if trigger_touched:
            try:
                ok = _rpc(host, port, "agent.set_trigger_name", [trigger_index, original_trigger_name])
                if ok is not True:
                    restore_errors.append("restore trigger returned False")
                else:
                    restored_trigger = _rpc(host, port, "agent.trigger_name", [trigger_index])
                    if restored_trigger != original_trigger_name:
                        restore_errors.append("restore trigger mismatch")
            except Exception as exc:
                restore_errors.append(f"restore trigger error: {exc}")

        if restore_errors:
            raise SmokeError("; ".join(restore_errors))

    return {
        "trigger_index": trigger_index,
        "original_trigger_name": original_trigger_name,
        "mutated_trigger_name": new_trigger_name,
    }


def _run_global_restore_cycle(
    host: str,
    port: int,
    global_name: str,
    global_value: Any,
) -> dict[str, Any]:
    original = _rpc(host, port, "agent.global_info", [global_name])
    _assert(isinstance(original, dict), "global_info returned non-dict")
    _assert(isinstance(original.get("index"), int), "global_info missing integer index")

    original_value = original.get("value")
    if original.get("array") is True:
        raise SmokeError("refusing to mutate array global in smoke")

    map_path = _rpc(host, port, "editor.current_map_path")
    global_touched = False
    body_error: Exception | None = None
    restore_errors: list[str] = []

    try:
        write_ok = _rpc(
            host, port, "agent.set_global_value_by_name", [global_name, global_value]
        )
        _assert(write_ok is True, "global restore write returned False")
        global_touched = True

        if not is_lni_marker_path(map_path):
            save_result = _rpc(host, port, "editor.save_map", [], timeout=60.0)
            _assert(isinstance(save_result, dict), f"editor.save_map returned {save_result!r}")
            _wait_for_server(host, port, 30.0, 0.5)

        after_write = _rpc(host, port, "agent.global_value", [original["index"]])
        _assert(
            normalize_live_value(after_write) == normalize_live_value(global_value),
            f"global readback mismatch: expected {global_value!r}, got {after_write!r}",
        )
    except Exception as exc:
        body_error = exc
    finally:
        if global_touched:
            try:
                restore_ok = _rpc(
                    host, port, "agent.set_global_value_by_name", [global_name, original_value]
                )
                if restore_ok is not True:
                    restore_errors.append("global restore write returned False")
                else:
                    if not is_lni_marker_path(map_path):
                        save_result = _rpc(
                            host, port, "editor.save_map", [], timeout=60.0
                        )
                        if not isinstance(save_result, dict):
                            restore_errors.append(f"editor.save_map returned {save_result!r}")
                        else:
                            _wait_for_server(host, port, 30.0, 0.5)

                    if not restore_errors:
                        after_restore = _rpc(host, port, "agent.global_value", [original["index"]])
                        if normalize_live_value(after_restore) != normalize_live_value(original_value):
                            restore_errors.append(
                                "global restore verification mismatch: "
                                f"expected {original_value!r}, got {after_restore!r}"
                            )
            except Exception as exc:
                restore_errors.append(f"global restore error: {exc}")

    if restore_errors:
        if body_error is not None:
            raise SmokeError(f"{body_error}; {'; '.join(restore_errors)}")
        raise SmokeError("; ".join(restore_errors))
    if body_error is not None:
        raise body_error

    return {
        "global_name": global_name,
        "index": original["index"],
        "mutated_value": global_value,
        "restore_value": original_value,
    }


def run(args: argparse.Namespace) -> dict[str, Any]:
    result: dict[str, Any] = {}

    status = _wait_for_server(args.host, args.port, args.wait, args.interval)
    result["status"] = status

    smoke = _rpc(args.host, args.port, "diag.smoke")
    _assert(isinstance(smoke, dict), "diag.smoke returned non-object")
    _assert(smoke.get("ok") is True, f"diag.smoke failed: {smoke!r}")
    result["smoke"] = smoke

    refresh_count = _rpc(args.host, args.port, "agent.refresh")
    triggers = _rpc(args.host, args.port, "agent.list_triggers") or []
    globals_list = _rpc(
        args.host, args.port, "agent.list_globals", timeout=_RPC_TIMEOUT_SLOW
    ) or []
    _assert(isinstance(triggers, list), "agent.list_triggers returned non-list")
    _assert(isinstance(globals_list, list), "agent.list_globals returned non-list")

    result["refresh_count"] = refresh_count
    result["trigger_count"] = len(triggers)
    result["global_count"] = len(globals_list)

    if args.restore:
        if len(triggers) == 0:
            raise SmokeError("refuse destructive smoke: require at least 1 trigger")
        _assert(0 <= args.trigger_index < len(triggers), "trigger index out of range")
        result["restore_cycle"] = _run_restore_cycle(
            args.host,
            args.port,
            args.trigger_index,
        )
    if args.restore_global:
        if not args.global_name:
            raise SmokeError("--restore-global requires --global-name")
        if args.global_value is None:
            raise SmokeError("--restore-global requires --global-value")
        result["global_restore_cycle"] = _run_global_restore_cycle(
            args.host,
            args.port,
            args.global_name,
            parse_json_arg(args.global_value),
        )

    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="YDWE Agent runtime smoke test")
    parser.add_argument("--host", default="127.0.0.1", help="JSON-RPC host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=27118, help="JSON-RPC port (default: 27118)")
    parser.add_argument("--wait", type=float, default=30.0, help="wait timeout seconds (default: 30)")
    parser.add_argument("--interval", type=float, default=1.0, help="poll interval seconds (default: 1)")
    parser.add_argument(
        "--restore",
        action="store_true",
        help="mutate one trigger name, verify, and restore the original value",
    )
    parser.add_argument(
        "--restore-global",
        action="store_true",
        help="mutate one global by name, verify, and restore the original value",
    )
    parser.add_argument("--global-name", help="global name for --restore-global")
    parser.add_argument(
        "--global-value",
        help="new global value for --restore-global (json-typed)",
    )
    parser.add_argument("--trigger-index", type=int, default=0, help="trigger index for --restore (default: 0)")
    args = parser.parse_args()

    try:
        report = run(args)
        print(f"PASS: status ok, trigger_count={report['trigger_count']}, global_count={report['global_count']}")
        if args.restore:
            cycle = report["restore_cycle"]
            print(
                "PASS: restore cycle "
                f"(trigger#{cycle['trigger_index']})"
            )
        if args.restore_global:
            cycle = report["global_restore_cycle"]
            print(
                "PASS: global restore cycle "
                f"({cycle['global_name']}@{cycle['index']})"
            )
        return 0
    except Exception as exc:
        print(f"FAIL: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
