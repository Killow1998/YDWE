#!/usr/bin/env python3
"""YDWE Agent JSON-RPC 2.0 Client

Connect to YDWE's YDAgentServer (TCP 127.0.0.1:27118) and call Agent API methods.
Use by external AI agent processes to read/modify WE GUI triggers.

Example:
    python ydagent_client.py refresh
    python ydagent_client.py status
    python ydagent_client.py smoke
    python ydagent_client.py list_triggers
    python ydagent_client.py get_eca_tree 0
    python ydagent_client.py set_trigger_name 0 "MyTrigger"
    python ydagent_client.py add_eca 0 2   # add action to trigger 0
    python ydagent_client.py rpc ai.status
"""

import json
import socket
import sys

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 27118


def rpc_call(host, port, method, params=None):
    """Send JSON-RPC 2.0 request and return result."""
    request = {"jsonrpc": "2.0", "id": 1, "method": method, "params": params or []}
    payload = json.dumps(request)

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(10)
    sock.connect((host, port))
    sock.sendall(payload.encode("utf-8"))

    # Read response
    data = b""
    while True:
        try:
            chunk = sock.recv(8192)
            if not chunk:
                break
            data += chunk
            if len(data) > 10 * 1024 * 1024:  # 10MB limit
                break
        except socket.timeout:
            break
    sock.close()

    response = json.loads(data.decode("utf-8"))
    if "error" in response:
        raise RuntimeError(
            f"JSON-RPC error {response['error']['code']}: {response['error']['message']}"
        )
    return response.get("result")


def pretty(obj, indent=0):
    """Pretty-print a nested table."""
    prefix = "  " * indent
    if isinstance(obj, dict):
        for k, v in obj.items():
            if isinstance(v, (dict, list)):
                print(f"{prefix}{k}:")
                pretty(v, indent + 1)
            else:
                print(f"{prefix}{k}: {v}")
    elif isinstance(obj, list):
        for i, item in enumerate(obj):
            if isinstance(item, (dict, list)):
                print(f"{prefix}[{i}]:")
                pretty(item, indent + 1)
            else:
                print(f"{prefix}[{i}] {item}")


def parse_json_arg(value):
    try:
        return json.loads(value)
    except Exception:
        return value


def print_smoke(result):
    print("OK" if result.get("ok") else "FAIL")
    for item in result.get("checks", []):
        state = "OK" if item.get("ok") else "FAIL"
        print(f"{state}: {item.get('name')}")
        if item.get("error"):
            print(f"  error: {item.get('error')}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return

    cmd = sys.argv[1]
    host = DEFAULT_HOST
    port = DEFAULT_PORT

    try:
        if cmd == "status":
            result = rpc_call(host, port, "diag.status")
            pretty(result)

        elif cmd == "smoke":
            result = rpc_call(host, port, "diag.smoke")
            print_smoke(result)

        elif cmd == "rpc":
            method = sys.argv[2]
            params = [parse_json_arg(arg) for arg in sys.argv[3:]]
            result = rpc_call(host, port, method, params)
            pretty(result)

        elif cmd == "refresh":
            result = rpc_call(host, port, "agent.refresh")
            print(f"OK: {result} triggers found")

        elif cmd == "list_triggers":
            result = rpc_call(host, port, "agent.list_triggers")
            pretty(result)

        elif cmd == "get_eca_tree":
            idx = int(sys.argv[2]) if len(sys.argv) > 2 else 0
            result = rpc_call(host, port, "agent.get_eca_tree", [idx])
            pretty(result)

        elif cmd == "dump_all":
            result = rpc_call(host, port, "agent.dump_all")
            pretty(result)

        elif cmd == "set_trigger_name":
            idx = int(sys.argv[2])
            name = sys.argv[3]
            result = rpc_call(host, port, "agent.set_trigger_name", [idx, name])
            print(f"OK" if result else "FAIL")

        elif cmd == "set_trigger_disabled":
            idx = int(sys.argv[2])
            disabled = sys.argv[3].lower() in ("1", "true", "yes")
            result = rpc_call(
                host, port, "agent.set_trigger_disabled", [idx, 1 if disabled else 0]
            )
            print(f"OK" if result else "FAIL")

        elif cmd == "add_eca":
            idx = int(sys.argv[2])
            eca_type = int(sys.argv[3])
            result = rpc_call(host, port, "agent.add_eca", [idx, eca_type])
            print(f"OK" if result else "FAIL")

        elif cmd == "remove_eca":
            idx = int(sys.argv[2])
            eca_type = int(sys.argv[3])
            eca_idx = int(sys.argv[4])
            result = rpc_call(host, port, "agent.remove_eca", [idx, eca_type, eca_idx])
            print(f"OK" if result else "FAIL")

        elif cmd == "set_eca_param":
            idx = int(sys.argv[2])
            eca_type = int(sys.argv[3])
            eca_idx = int(sys.argv[4])
            param_idx = int(sys.argv[5])
            value = sys.argv[6]
            result = rpc_call(
                host, port, "agent.set_eca_param_value",
                [idx, eca_type, eca_idx, param_idx, value],
            )
            print(f"OK" if result else "FAIL")

        # ---- Object editor commands ----
        elif cmd == "object_read":
            obj_type = sys.argv[2]
            map_path = sys.argv[3]
            result = rpc_call(host, port, "object.read", [obj_type, map_path])
            if isinstance(result, str):
                # Parse and pretty-print JSON for readability
                try:
                    pretty(json.loads(result))
                except Exception:
                    print(result)
            else:
                pretty(result)

        elif cmd == "object_write":
            obj_type = sys.argv[2]
            map_path = sys.argv[3]
            # Read JSON from file or stdin
            json_path = sys.argv[4] if len(sys.argv) > 4 else None
            if json_path:
                with open(json_path, "r") as f:
                    json_data = f.read()
            else:
                json_data = sys.stdin.read()
            result = rpc_call(host, port, "object.write", [obj_type, map_path, json_data])
            print("OK" if result else "FAIL")

        else:
            print(f"Unknown command: {cmd}")
            print(f"Diagnostic commands: status, smoke, rpc <method> [json_params...]")
            print(f"Trigger commands: refresh, list_triggers, get_eca_tree, dump_all, ")
            print(f"  set_trigger_name, set_trigger_disabled, add_eca, remove_eca, set_eca_param")
            print(f"Object commands: object_read <type> <map_path>")
            print(f"  object_write <type> <map_path> [json_file]")
            print(f"  Types: unit, item, buff, doodad, ability, hero, upgrade")

    except ConnectionRefusedError:
        print(
            f"ERROR: Cannot connect to {host}:{port}. Is YDWE running with YDAgentServer enabled?"
        )
    except Exception as e:
        print(f"ERROR: {e}")


if __name__ == "__main__":
    main()
