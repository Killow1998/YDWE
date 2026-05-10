#!/usr/bin/env python3
"""YDWE Agent live regression harness."""

from __future__ import annotations

import argparse
import csv
import io
import json
import subprocess
import time
from dataclasses import dataclass
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


@dataclass
class LaunchedSession:
    proc: subprocess.Popen[Any]
    editor_pids_before: set[int]


_DEFAULT_YDWE_EXE = Path(r"Q:\AppData\ydwe\YDWE\Build\publish\Debug\YDWE.exe")
_DEFAULT_MAP = Path(r"Q:\AppData\ydwe\work\compose_demo_gui_only_v3.w3x")
_DEFAULT_HOST = "127.0.0.1"
_DEFAULT_PORT = 27118
_DEFAULT_GLOBAL_NAME = "udg_compose_count"
_DEFAULT_GLOBAL_VALUE = "17"
_DEFAULT_RPC_TIMEOUT = 60.0


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


def _parse_check_global(spec: str) -> tuple[str, Any]:
    if "=" not in spec:
        raise RegressionError(f"invalid --check-global value {spec!r}, expected NAME=JSON_VALUE")
    name, value = spec.split("=", 1)
    name = name.strip()
    if not name:
        raise RegressionError("invalid --check-global value with empty name")
    return name, parse_json_arg(value)


def _collect_global_checks(
    default_name: str,
    default_value: Any,
    explicit: list[str] | None,
) -> list[tuple[str, Any]]:
    if explicit:
        checks = [_parse_check_global(item) for item in explicit]
        if checks:
            return checks
    return [(default_name, default_value)]


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


def _process_ids_by_image(image_name: str) -> set[int]:
    try:
        output = subprocess.check_output(
            ["tasklist", "/FO", "CSV", "/NH", "/FI", f"IMAGENAME eq {image_name}"],
            text=True,
            stderr=subprocess.DEVNULL,
        )
    except Exception:
        return set()
    pids: set[int] = set()
    for row in csv.reader(io.StringIO(output)):
        if len(row) < 2:
            continue
        try:
            pids.add(int(row[1]))
        except ValueError:
            continue
    return pids


def _agent_is_available(host: str, port: int, timeout: float = 1.0) -> bool:
    try:
        status = _rpc(host, port, "diag.status", timeout=timeout)
        return isinstance(status, dict) and status.get("ok") is True
    except Exception:
        return False


def _close_launched_process(session: LaunchedSession) -> None:
    proc = session.proc
    if proc.poll() is not None:
        pass
    else:
        proc.terminate()
        try:
            proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            proc.kill()

    current_editor_pids = _process_ids_by_image("worldeditydwe.exe")
    spawned_editor_pids = sorted(current_editor_pids - session.editor_pids_before)
    for pid in spawned_editor_pids:
        try:
            subprocess.run(
                ["taskkill", "/PID", str(pid), "/T", "/F"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            )
        except Exception:
            pass
        if pid in _process_ids_by_image("worldeditydwe.exe"):
            try:
                subprocess.run(
                    [
                        "powershell",
                        "-NoProfile",
                        "-Command",
                        f"Stop-Process -Id {pid} -Force",
                    ],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    check=False,
                )
            except Exception:
                pass


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


def _wait_for_current_map_path(
    host: str,
    port: int,
    target_map: str,
    wait_seconds: float,
    interval_seconds: float = 0.5,
) -> str:
    deadline = time.time() + wait_seconds
    last_map = ""
    while time.time() < deadline:
        current_map = _read_current_map_path(host, port)
        last_map = current_map
        if _normalize_path(current_map) == target_map:
            return current_map
        time.sleep(interval_seconds)
    raise RegressionError(
        f"map mismatch (requested={target_map}, current={last_map})"
    )


def _run_save_map(host: str, port: int, timeout: float) -> None:
    result = _rpc(host, port, "editor.save_map", timeout=timeout)
    _assert(isinstance(result, dict), f"editor.save_map returned unexpected value: {result!r}")
    _wait_for_editor_server(host, port, 30.0)


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


def _read_global_info(
    host: str,
    port: int,
    global_name: str,
    rpc_timeout: float,
    wait_seconds: float = 90.0,
) -> dict[str, Any]:
    deadline = time.time() + wait_seconds
    last_error = ""
    while time.time() < deadline:
        try:
            info = _rpc(host, port, "agent.global_info", [global_name], timeout=rpc_timeout)
            if isinstance(info, dict) and isinstance(info.get("index"), int):
                return info
            last_error = f"unexpected response: {info!r}"
        except Exception as exc:
            last_error = str(exc)
        time.sleep(0.5)
    raise RegressionError(f"global not ready: {global_name}: {last_error}")


def _value_for_writeback(info: dict[str, Any]) -> Any:
    value = info.get("value")
    type_name = str(info.get("type_name") or "").lower()
    if type_name == "string" and isinstance(value, str):
        text = value.strip()
        if len(text) >= 2 and text[0] == '"' and text[-1] == '"':
            try:
                decoded = json.loads(text)
                if isinstance(decoded, str):
                    return decoded
            except Exception:
                pass
    return value


def _global_scalar_cycle(
    host: str,
    port: int,
    global_name: str,
    global_value: Any,
    rpc_timeout: float,
    save_timeout: float,
) -> None:
    info = _read_global_info(host, port, global_name, rpc_timeout)
    _assert(isinstance(info, dict), "agent.global_info returned non-dict")
    _assert(isinstance(info.get("index"), int), "global_info missing integer index")
    _assert(not info.get("array"), "refusing non-scalar global")

    original_value = _value_for_writeback(info)
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
            map_path = _read_current_map_path(host, port)
            if not is_lni_marker_path(map_path):
                clear_ok = _rpc(
                    host,
                    port,
                    "agent.clear_pending_globals",
                    [map_path, global_name],
                    timeout=rpc_timeout,
                )
                _assert(clear_ok is True, "failed to clear restore pending override")
            restored_value = _rpc(host, port, "agent.global_value", [index], timeout=rpc_timeout)
            _assert(
                normalize_live_value(restored_value) == normalize_live_value(original_value),
                f"global restore mismatch, expected {original_value!r}, got {restored_value!r}",
            )


def _pending_entry_name_variants(global_name: str) -> list[str]:
    if global_name.startswith("udg_"):
        return [global_name, global_name[4:]]
    return [global_name, f"udg_{global_name}"]


def _pending_contains(pending: dict[str, Any], global_name: str) -> bool:
    if global_name in pending:
        return True
    for variant in _pending_entry_name_variants(global_name):
        if variant != global_name and variant in pending:
            return True
    return False


def _pending_entries_for_map(host: str, port: int, map_path: str, rpc_timeout: float) -> dict[str, Any]:
    pending = _rpc(host, port, "agent.list_pending_globals", [map_path], timeout=rpc_timeout)
    if pending is None:
        return {}
    if pending == []:
        return {}
    _assert(isinstance(pending, dict), "agent.list_pending_globals returned non-dict")
    return pending


def _build_temporary_trigger_name(original_name: str, trigger_index: int) -> str:
    marker = f"__AI_RENAME_{trigger_index}_{int(time.time() * 1000)}"
    max_len = 255
    if len(marker) >= max_len:
        return marker[:max_len]
    base = original_name if original_name else f"trigger_{trigger_index}"
    suffix = f"_{marker}"
    limit = max_len - len(suffix)
    if limit <= 0:
        return marker[:max_len]
    return f"{base[:limit]}{suffix}"


def _run_trigger_rename_regression(
    host: str,
    port: int,
    trigger_index: int,
    rpc_timeout: float,
) -> None:
    triggers = _rpc(host, port, "agent.list_triggers", timeout=rpc_timeout)
    _assert(isinstance(triggers, list), "agent.list_triggers returned non-list")
    _assert(
        0 <= trigger_index < len(triggers),
        f"trigger index out of range: {trigger_index} (count={len(triggers)})",
    )

    original_trigger_name = _rpc(
        host,
        port,
        "agent.trigger_name",
        [trigger_index],
        timeout=rpc_timeout,
    )
    _assert(
        isinstance(original_trigger_name, str) and original_trigger_name != "",
        "agent.trigger_name returned empty value",
    )

    temporary_name = _build_temporary_trigger_name(original_trigger_name, trigger_index)
    mutated = False
    body_error: Exception | None = None
    restore_errors: list[str] = []

    try:
        set_ok = _rpc(
            host,
            port,
            "agent.set_trigger_name",
            [trigger_index, temporary_name],
            timeout=rpc_timeout,
        )
        _assert(set_ok is True, "agent.set_trigger_name returned False")
        mutated = True

        verify = _rpc(
            host,
            port,
            "agent.trigger_name",
            [trigger_index],
            timeout=rpc_timeout,
        )
        _assert(verify == temporary_name, "trigger rename verification failed")
        print(
            f"PASS: trigger_rename_set index={trigger_index} "
            f"temp_name={temporary_name}"
        )
    except Exception as exc:  # noqa: BLE001
        body_error = exc
    finally:
        if mutated:
            try:
                restore_ok = _rpc(
                    host,
                    port,
                    "agent.set_trigger_name",
                    [trigger_index, original_trigger_name],
                    timeout=rpc_timeout,
                )
                if restore_ok is not True:
                    restore_errors.append("restore trigger rename returned False")
                else:
                    restored = _rpc(
                        host,
                        port,
                        "agent.trigger_name",
                        [trigger_index],
                        timeout=rpc_timeout,
                    )
                    if restored != original_trigger_name:
                        restore_errors.append("restore trigger name mismatch")
            except Exception as exc:
                restore_errors.append(f"restore trigger rename error: {exc}")

    if restore_errors:
        if body_error is not None:
            raise RegressionError(f"{body_error}; {'; '.join(restore_errors)}")
        raise RegressionError("; ".join(restore_errors))
    if body_error is not None:
        raise body_error

    print(
        f"PASS: trigger_rename_restore index={trigger_index} "
        f"original_name={original_trigger_name}"
    )


def _run_object_read_check(
    host: str,
    port: int,
    object_type: str,
    map_path: str,
    rpc_timeout: float,
) -> None:
    result = _rpc(
        host,
        port,
        "object.read",
        [object_type, map_path],
        timeout=rpc_timeout,
    )
    if isinstance(result, str):
        try:
            result = json.loads(result)
        except Exception as exc:
            raise RegressionError(f"object.read({object_type}) returned non-JSON string: {exc}")

    _assert(isinstance(result, (dict, list)), f"object.read({object_type}) returned non-object payload")
    count = len(result)
    _assert(count > 0, f"object.read({object_type}) returned empty data")
    print(f"PASS: object_read type={object_type} count={count}")


def _decode_object_payload(result: Any, object_type: str) -> dict[str, Any]:
    if isinstance(result, str):
        try:
            result = json.loads(result)
        except Exception as exc:
            raise RegressionError(f"object.read({object_type}) returned non-JSON string: {exc}")
    _assert(isinstance(result, dict), f"object.read({object_type}) returned non-dict payload")
    return result


def _object_records(data: dict[str, Any]) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for key in ("custom", "original"):
        value = data.get(key)
        if isinstance(value, list):
            records.extend(record for record in value if isinstance(record, dict))
    return records


def _set_object_field(record: dict[str, Any], field_id: str, value: str) -> None:
    fields = record.setdefault("fields", {})
    _assert(isinstance(fields, dict), "object record fields is not a dict")
    fields[field_id] = value

    details = record.setdefault("field_details", [])
    _assert(isinstance(details, list), "object record field_details is not a list")
    for detail in details:
        if isinstance(detail, dict) and detail.get("id") == field_id:
            detail["type"] = 3
            detail["value"] = value
            return
    details.append({
        "id": field_id,
        "type": 3,
        "level": 0,
        "data": 0,
        "terminator": 0,
        "value": value,
    })


def _pick_string_object_field(records: list[dict[str, Any]]) -> tuple[dict[str, Any], str, str] | None:
    candidates: list[tuple[int, dict[str, Any], str, str]] = []
    preferred = {"unam", "anam", "gnam", "fnam"}
    for record in records:
        fields = record.get("fields")
        if not isinstance(fields, dict):
            continue
        for field_id, value in fields.items():
            if not isinstance(field_id, str) or not isinstance(value, str):
                continue
            if field_id in preferred:
                rank = 0
            elif value != "":
                rank = 1
            else:
                rank = 2
            candidates.append((rank, record, field_id, value))
    if not candidates:
        return None
    candidates.sort(key=lambda item: item[0])
    _rank, record, field_id, value = candidates[0]
    return record, field_id, value


def _run_object_write_regression(
    host: str,
    port: int,
    object_type: str,
    map_path: str,
    rpc_timeout: float,
    save_timeout: float,
) -> None:
    original = _decode_object_payload(
        _rpc(host, port, "object.read", [object_type, map_path], timeout=rpc_timeout),
        object_type,
    )
    records = _object_records(original)
    _assert(records, f"object.write({object_type}) has no records to mutate")

    picked = _pick_string_object_field(records)
    _assert(picked is not None, f"object.write({object_type}) found no string field")
    target_record, field_id, original_value = picked

    marker = f"{original_value}_YDAGENT_OBJECT_WRITE"
    mutated = json.loads(json.dumps(original, ensure_ascii=False))
    mutated_record = None
    for record in _object_records(mutated):
        if record.get("id") == target_record.get("id"):
            mutated_record = record
            break
    _assert(mutated_record is not None, "object.write target record disappeared during copy")

    restore_errors: list[str] = []
    body_error: Exception | None = None
    try:
        _set_object_field(mutated_record, field_id, marker)
        ok = _rpc(
            host,
            port,
            "object.write",
            [object_type, map_path, json.dumps(mutated, ensure_ascii=False, separators=(",", ":"))],
            timeout=rpc_timeout,
        )
        _assert(ok is True, f"object.write({object_type}) returned {ok!r}")
        _run_save_map(host, port, save_timeout)
        time.sleep(2)

        after = _decode_object_payload(
            _rpc(host, port, "object.read", [object_type, map_path], timeout=rpc_timeout),
            object_type,
        )
        after_record = next((r for r in _object_records(after) if r.get("id") == target_record.get("id")), None)
        _assert(after_record is not None, "object.write target record missing after write")
        _assert(
            after_record.get("fields", {}).get(field_id) == marker,
            f"object.write({object_type}) readback mismatch",
        )
    except Exception as exc:
        body_error = exc
    finally:
        try:
            ok = _rpc(
                host,
                port,
                "object.write",
                [object_type, map_path, json.dumps(original, ensure_ascii=False, separators=(",", ":"))],
                timeout=rpc_timeout,
            )
            if ok is not True:
                restore_errors.append(f"object restore write returned {ok!r}")
            else:
                _run_save_map(host, port, save_timeout)
                time.sleep(2)
        except Exception as exc:
            restore_errors.append(f"object restore error: {exc}")

    if restore_errors:
        if body_error is not None:
            raise RegressionError(f"{body_error}; {'; '.join(restore_errors)}")
        raise RegressionError("; ".join(restore_errors))
    if body_error is not None:
        raise body_error

    print(f"PASS: object_write_restore type={object_type} field={field_id}")


def _run_object_field_map_check(
    host: str,
    port: int,
    object_type: str,
    rpc_timeout: float,
) -> None:
    result = _rpc(
        host,
        port,
        "object.field_map",
        [object_type],
        timeout=rpc_timeout,
    )
    _assert(isinstance(result, (dict, list)), f"object.field_map({object_type}) returned non-object payload")
    count = len(result)
    _assert(count > 0, f"object.field_map({object_type}) returned empty data")
    print(f"PASS: object_field_map type={object_type} count={count}")


def _list_scalar_globals(host: str, port: int, rpc_timeout: float) -> list[str]:
    globals_list = _rpc(host, port, "agent.list_globals", timeout=rpc_timeout)
    _assert(isinstance(globals_list, list), "agent.list_globals returned non-list")
    names: list[str] = []
    for item in globals_list:
        if not isinstance(item, dict):
            continue
        name = item.get("name")
        if not isinstance(name, str) or not name:
            continue
        if item.get("array") is True:
            continue
        names.append(name)
    return names


def _resolve_pending_global_names(
    host: str,
    port: int,
    preferred_names: list[str],
    rpc_timeout: float,
) -> list[str]:
    available = _list_scalar_globals(host, port, rpc_timeout)
    selected: list[str] = []
    for preferred in preferred_names:
        for variant in _pending_entry_name_variants(preferred):
            if variant in available and variant not in selected:
                selected.append(variant)
                break

    for item in available:
        if item in selected:
            continue
        selected.append(item)
        if len(selected) >= 2:
            break

    if len(selected) < 2:
        raise RegressionError(
            f"pending clear check needs at least two scalar globals, found only {len(selected)}"
        )
    return selected[:2]


def _to_float_value(value: Any) -> float | None:
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, str):
        stripped = value.strip()
        if not stripped:
            return None
        try:
            return float(stripped)
        except ValueError:
            return None
    return None


def _to_bool_value(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        text = value.strip().lower()
        if text in {"true", "1", "yes", "on"}:
            return True
        if text in {"false", "0", "no", "off"}:
            return False
    return bool(value)


def _build_pending_value(info: dict[str, Any], index: int) -> Any:
    value = info.get("value")
    type_name = str(info.get("type_name") or "").lower()

    if type_name == "boolean":
        return not _to_bool_value(value)
    if type_name == "integer":
        current = _to_float_value(value)
        if current is None:
            return index + 1
        return int(current) + 1
    if type_name == "real":
        current = _to_float_value(value)
        if current is None:
            return float(index) + 1.25
        return current + 1.0
    if type_name == "string":
        base = "" if value is None else str(value)
        return f"{base}_pending_{index + 1}"

    current = _to_float_value(value)
    if current is not None:
        return current + 1
    return f"pending_value_{index + 1}"


def _global_snapshot(
    host: str,
    port: int,
    global_name: str,
    rpc_timeout: float,
) -> tuple[str, int, Any]:
    info = _read_global_info(host, port, global_name, rpc_timeout)
    _assert(not info.get("array"), f"refusing non-scalar global: {global_name}")
    name = info.get("name")
    if isinstance(name, str):
        global_name = name
    return global_name, int(info["index"]), _value_for_writeback(info)


def _run_pending_clear_regression(
    host: str,
    port: int,
    preferred_names: list[str],
    rpc_timeout: float,
    save_timeout: float,
) -> None:
    map_path = _read_current_map_path(host, port)
    _assert(not is_lni_marker_path(map_path), "pending clear regression cannot run on LNI marker sessions")

    pending_names = _resolve_pending_global_names(host, port, preferred_names, rpc_timeout)
    snapshots = [_global_snapshot(host, port, name, rpc_timeout) for name in pending_names]

    staged: list[str] = []
    body_error: Exception | None = None
    restore_errors: list[str] = []

    try:
        for idx, (global_name, _index, _original) in enumerate(snapshots):
            info = _rpc(host, port, "agent.global_info", [global_name], timeout=rpc_timeout)
            _assert(isinstance(info, dict), f"agent.global_info({global_name}) returned non-dict")
            test_value = _build_pending_value(info, idx)
            ok = _rpc(
                host,
                port,
                "agent.set_global_value_by_name",
                [global_name, test_value],
                timeout=rpc_timeout,
            )
            _assert(ok is True, f"agent.set_global_value_by_name({global_name}) returned False")
            staged.append(global_name)

        pending = _pending_entries_for_map(host, port, map_path, rpc_timeout)
        for global_name, _, _ in snapshots:
            _assert(
                _pending_contains(pending, global_name),
                f"pending entry missing after staging for {global_name}",
            )
        print(
            f"PASS: pending_clear_staged "
            f"map={map_path} globals={[name for name, _index, _original in snapshots]}"
        )

        clear_target = snapshots[0][0]
        keep_target = snapshots[1][0]
        clear_one_ok = _rpc(
            host,
            port,
            "agent.clear_pending_globals",
            [map_path, clear_target],
            timeout=rpc_timeout,
        )
        _assert(clear_one_ok is True, f"clear_pending_globals({clear_target!r}) returned False")
        after_clear_one = _pending_entries_for_map(host, port, map_path, rpc_timeout)
        _assert(
            not _pending_contains(after_clear_one, clear_target),
            f"pending entry still present after single clear: {clear_target}",
        )
        _assert(
            _pending_contains(after_clear_one, keep_target),
            f"pending entry missing after single clear: {keep_target}",
        )
        print(f"PASS: pending_clear_single map={map_path} cleared={clear_target}")

        clear_all_ok = _rpc(
            host,
            port,
            "agent.clear_pending_globals",
            [map_path, "*"],
            timeout=rpc_timeout,
        )
        _assert(clear_all_ok is True, "clear_pending_globals('*') returned False")
        after_clear_all = _pending_entries_for_map(host, port, map_path, rpc_timeout)
        _assert(
            len(after_clear_all) == 0,
            f"pending entries remain after clear all: {after_clear_all!r}",
        )
        print(f"PASS: pending_clear_all map={map_path}")
    except Exception as exc:  # noqa: BLE001
        body_error = exc
    finally:
        if staged:
            try:
                clear_all_ok = _rpc(
                    host,
                    port,
                    "agent.clear_pending_globals",
                    [map_path, "*"],
                    timeout=rpc_timeout,
                )
                if clear_all_ok is not True:
                    restore_errors.append("failed to clear pending overrides during cleanup")
            except Exception as exc:
                restore_errors.append(f"cleanup clear failed: {exc}")

            for global_name, index, original_value in snapshots:
                if global_name not in staged:
                    continue
                try:
                    _run_restore(host, port, global_name, original_value, rpc_timeout)
                    clear_ok = _rpc(
                        host,
                        port,
                        "agent.clear_pending_globals",
                        [map_path, global_name],
                        timeout=rpc_timeout,
                    )
                    if clear_ok is not True:
                        restore_errors.append(f"failed to clear restore pending override for {global_name}")
                    restored = _rpc(host, port, "agent.global_value", [index], timeout=rpc_timeout)
                    if normalize_live_value(restored) != normalize_live_value(original_value):
                        restore_errors.append(f"restore mismatch for {global_name}: {restored!r}")
                except Exception as exc:
                    restore_errors.append(f"restore failed for {global_name}: {exc}")

            if not is_lni_marker_path(map_path):
                try:
                    _run_save_map(host, port, save_timeout)
                except Exception as exc:
                    restore_errors.append(f"restore save failed: {exc}")

    if restore_errors:
        if body_error is not None:
            raise RegressionError(f"{body_error}; {'; '.join(restore_errors)}")
        raise RegressionError("; ".join(restore_errors))
    if body_error is not None:
        raise body_error


def run(args: argparse.Namespace) -> LaunchedSession | None:
    map_path = args.map_path
    target_map = _normalize_path(map_path)
    launched: LaunchedSession | None = None

    if args.no_launch and args.close_launched:
        print("INFO: --close-launched ignored when --no-launch is set")

    if not args.no_launch:
        if _agent_is_available(args.host, args.port):
            raise RegressionError(
                "Agent server is already running; use --no-launch for the current "
                "session or close the existing YDWE session before launching"
            )
        editor_pids_before = _process_ids_by_image("worldeditydwe.exe")
        proc = _launch_ydwe(args.ydwe_exe, map_path)
        launched = LaunchedSession(proc=proc, editor_pids_before=editor_pids_before)
        print(f"PASS: launched_pid={proc.pid}")
        time.sleep(0.5)

    _wait_for_editor_server(args.host, args.port, args.wait)
    print(f"PASS: server_ready host={args.host} port={args.port}")

    _wait_for_current_map_path(
        args.host,
        args.port,
        target_map,
        args.wait,
    )
    print("PASS: map_path_verified")

    _run_save_map(args.host, args.port, args.save_timeout)
    print("PASS: editor.save_map_ok")

    for global_name, global_value in args.global_checks:
        _global_scalar_cycle(
            args.host,
            args.port,
            global_name,
            global_value,
            args.rpc_timeout,
            args.save_timeout,
        )
        print(
            f"PASS: global_scalar_restore name={global_name} "
            f"target={global_value!r}"
        )

    if args.check_pending_clear:
        preferred_names = [name for name, _value in args.global_checks]
        _run_pending_clear_regression(
            args.host,
            args.port,
            preferred_names,
            args.rpc_timeout,
            args.save_timeout,
        )

    if args.check_trigger_rename:
        _run_trigger_rename_regression(
            args.host,
            args.port,
            args.trigger_index,
            args.rpc_timeout,
        )

    if args.check_object_read:
        map_path = _read_current_map_path(args.host, args.port)
        for object_type in args.check_object_read:
            _run_object_read_check(
                args.host,
                args.port,
                object_type,
                map_path,
                args.rpc_timeout,
            )

    if args.check_object_write:
        map_path = _read_current_map_path(args.host, args.port)
        for object_type in args.check_object_write:
            _run_object_write_regression(
                args.host,
                args.port,
                object_type,
                map_path,
                args.rpc_timeout,
                args.save_timeout,
            )

    if args.check_object_field_map:
        for object_type in args.check_object_field_map:
            _run_object_field_map_check(
                args.host,
                args.port,
                object_type,
                args.rpc_timeout,
            )

    return launched


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
        "--check-global",
        action="append",
        default=[],
        metavar="NAME=JSON_VALUE",
        help="repeatable scalar global check in the form NAME=JSON_VALUE",
    )
    parser.add_argument(
        "--check-pending-clear",
        action="store_true",
        help="run pending global clear/restore regression on current map",
    )
    parser.add_argument(
        "--check-trigger-rename",
        action="store_true",
        help="rename one trigger, verify, and restore on current map",
    )
    parser.add_argument(
        "--check-object-read",
        action="append",
        metavar="TYPE",
        help="read object data without mutating; repeatable for multiple types",
    )
    parser.add_argument(
        "--check-object-write",
        action="append",
        metavar="TYPE",
        help="mutate one string object field, save, verify, and restore; repeatable",
    )
    parser.add_argument(
        "--check-object-field-map",
        action="append",
        metavar="TYPE",
        help="read object-editor field metadata without mutating; repeatable for multiple types",
    )
    parser.add_argument(
        "--trigger-index",
        type=int,
        default=0,
        help="trigger index for --check-trigger-rename (default: 0)",
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
        default=_DEFAULT_RPC_TIMEOUT,
        help=f"timeout for non-save Agent RPC calls (default: {_DEFAULT_RPC_TIMEOUT:g})",
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
    args.global_checks = _collect_global_checks(args.global_name, args.global_value, args.check_global)
    launched_session: LaunchedSession | None = None

    try:
        launched_session = run(args)
        print("PASS: regression_complete")
        return 0
    except Exception as exc:  # noqa: BLE001
        print(f"FAIL: {exc}")
        return 1
    finally:
        if args.close_launched and launched_session is not None:
            _close_launched_process(launched_session)


if __name__ == "__main__":
    raise SystemExit(main())
