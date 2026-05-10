#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
import json
import os
import shutil
import sys
import time
import urllib.error
import urllib.request
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_REPO = "Killow1998/YDWE"
DEFAULT_WORKFLOW = "ydagent-native.yml"
DEFAULT_ARTIFACT = "ydagent-native-debug"
DEFAULT_OUTPUT = ROOT / "Build" / "artifacts" / DEFAULT_ARTIFACT
DEFAULT_RUNTIME_PLUGIN = ROOT / "Build" / "publish" / "Debug" / "plugin"
REQUIRED_YDTRIGGER_EXPORTS = {
    "ydt_get_eca_active",
    "ydt_add_eca",
    "ydt_remove_eca",
    "ydt_read_object_file",
    "ydt_write_object_file",
}


def _token() -> str:
    return os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN") or ""


def _request_json(url: str, token: str) -> dict:
    req = urllib.request.Request(url)
    req.add_header("Accept", "application/vnd.github+json")
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(req, timeout=60) as response:
        return json.loads(response.read().decode("utf-8"))


def _download(url: str, target: Path, token: str) -> None:
    req = urllib.request.Request(url)
    req.add_header("Accept", "application/vnd.github+json")
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    target.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(req, timeout=120) as response:
        with target.open("wb") as handle:
            shutil.copyfileobj(response, handle)


def _latest_successful_run(repo: str, workflow: str, token: str) -> dict:
    url = (
        f"https://api.github.com/repos/{repo}/actions/workflows/{workflow}/runs"
        "?branch=master&status=success&per_page=10"
    )
    data = _request_json(url, token)
    runs = data.get("workflow_runs")
    if not isinstance(runs, list) or not runs:
        raise RuntimeError(f"no successful runs found for {repo}/{workflow}")
    return runs[0]


def _find_artifact(repo: str, run_id: int, artifact_name: str, token: str) -> dict:
    data = _request_json(
        f"https://api.github.com/repos/{repo}/actions/runs/{run_id}/artifacts",
        token,
    )
    artifacts = data.get("artifacts")
    if not isinstance(artifacts, list):
        raise RuntimeError("artifact response did not contain an artifacts array")
    for artifact in artifacts:
        if artifact.get("name") == artifact_name and not artifact.get("expired"):
            return artifact
    raise RuntimeError(f"artifact not found or expired: {artifact_name}")


def _print_artifact_info(repo: str, workflow: str, artifact_name: str, token: str) -> None:
    run = _latest_successful_run(repo, workflow, token)
    run_id = int(run["id"])
    print(f"RUN: {run_id}")
    print(f"SHA: {run.get('head_sha')}")
    print(f"URL: {run.get('html_url')}")
    artifact = _find_artifact(repo, run_id, artifact_name, token)
    print(f"ARTIFACT: {artifact.get('name')}")
    print(f"SIZE: {artifact.get('size_in_bytes')}")
    print(f"EXPIRED: {artifact.get('expired')}")
    print(f"DOWNLOAD_API: {artifact.get('archive_download_url')}")


def _extract(zip_path: Path, output_dir: Path) -> None:
    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path) as archive:
        archive.extractall(output_dir)


def _read_c_string(data: bytes, offset: int) -> str:
    end = data.find(b"\0", offset)
    if end < 0:
        end = len(data)
    return data[offset:end].decode("ascii", errors="replace")


def _read_pe_exports(dll_path: Path) -> set[str]:
    data = dll_path.read_bytes()
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise RuntimeError(f"not a PE file: {dll_path}")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise RuntimeError(f"invalid PE signature: {dll_path}")

    coff = pe_offset + 4
    section_count = struct.unpack_from("<H", data, coff + 2)[0]
    optional_size = struct.unpack_from("<H", data, coff + 16)[0]
    optional = coff + 20
    magic = struct.unpack_from("<H", data, optional)[0]
    if magic == 0x10B:
        data_dir = optional + 96
    elif magic == 0x20B:
        data_dir = optional + 112
    else:
        raise RuntimeError(f"unsupported PE optional header magic: 0x{magic:x}")

    export_rva, _export_size = struct.unpack_from("<II", data, data_dir)
    if export_rva == 0:
        return set()

    sections: list[tuple[int, int, int, int]] = []
    section_offset = optional + optional_size
    for index in range(section_count):
        current = section_offset + index * 40
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", data, current + 8)
        sections.append((virtual_address, max(virtual_size, raw_size), raw_pointer, raw_size))

    def rva_to_offset(rva: int) -> int:
        for virtual_address, virtual_size, raw_pointer, raw_size in sections:
            if virtual_address <= rva < virtual_address + virtual_size:
                offset = raw_pointer + (rva - virtual_address)
                if offset >= raw_pointer + raw_size and raw_size != 0:
                    raise RuntimeError(f"RVA points outside raw section: 0x{rva:x}")
                return offset
        raise RuntimeError(f"RVA not found in PE sections: 0x{rva:x}")

    export_offset = rva_to_offset(export_rva)
    name_count = struct.unpack_from("<I", data, export_offset + 24)[0]
    names_rva = struct.unpack_from("<I", data, export_offset + 32)[0]
    names_offset = rva_to_offset(names_rva)
    exports: set[str] = set()
    for index in range(name_count):
        name_rva = struct.unpack_from("<I", data, names_offset + index * 4)[0]
        exports.add(_read_c_string(data, rva_to_offset(name_rva)))
    return exports


def _verify_ydtrigger(dll_path: Path) -> None:
    exports = _read_pe_exports(dll_path)
    missing = sorted(REQUIRED_YDTRIGGER_EXPORTS - exports)
    if missing:
        raise RuntimeError(f"YDTrigger.dll missing required exports: {', '.join(missing)}")
    print(f"VERIFIED: {dll_path} exports={len(exports)}")


def _install(output_dir: Path, runtime_plugin: Path) -> None:
    source = output_dir / "Development" / "Build" / "bin" / "Debug" / "plugin" / "YDTrigger.dll"
    if not source.exists():
        source = output_dir / "YDTrigger.dll"
    if not source.exists():
        raise RuntimeError(f"YDTrigger.dll not found in extracted artifact: {output_dir}")
    _verify_ydtrigger(source)

    runtime_plugin.mkdir(parents=True, exist_ok=True)
    target = runtime_plugin / "YDTrigger.dll"
    if target.exists():
        stamp = time.strftime("%Y%m%d_%H%M%S")
        backup = target.with_name(f"YDTrigger.dll.{stamp}.bak")
        shutil.copy2(target, backup)
        print(f"BACKUP: {backup}")
    shutil.copy2(source, target)
    print(f"INSTALLED: {target}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Download or install a YDWE Agent native CI artifact")
    parser.add_argument("--repo", default=DEFAULT_REPO, help=f"GitHub repo, owner/name (default: {DEFAULT_REPO})")
    parser.add_argument("--workflow", default=DEFAULT_WORKFLOW, help=f"workflow file name (default: {DEFAULT_WORKFLOW})")
    parser.add_argument("--artifact", default=DEFAULT_ARTIFACT, help=f"artifact name (default: {DEFAULT_ARTIFACT})")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT, help=f"extract output dir (default: {DEFAULT_OUTPUT})")
    parser.add_argument("--zip", type=Path, help="use an already downloaded artifact zip instead of GitHub API download")
    parser.add_argument("--info", action="store_true", help="print the latest successful run and artifact metadata without downloading")
    parser.add_argument("--install", action="store_true", help="install YDTrigger.dll into Build/publish/Debug/plugin after download")
    parser.add_argument("--verify-runtime", action="store_true", help="verify the currently installed Debug runtime YDTrigger.dll and exit")
    parser.add_argument("--runtime-plugin", type=Path, default=DEFAULT_RUNTIME_PLUGIN, help=f"runtime plugin dir (default: {DEFAULT_RUNTIME_PLUGIN})")
    args = parser.parse_args()

    try:
        if args.verify_runtime:
            _verify_ydtrigger(args.runtime_plugin / "YDTrigger.dll")
            return 0
        if args.info:
            _print_artifact_info(args.repo, args.workflow, args.artifact, _token())
            return 0
        if args.zip:
            zip_path = args.zip.resolve()
            if not zip_path.exists():
                raise RuntimeError(f"artifact zip not found: {zip_path}")
            print(f"ZIP: {zip_path}")
        else:
            token = _token()
            if not token:
                print("FAIL: set GH_TOKEN or GITHUB_TOKEN with Actions artifact read access, or pass --zip")
                return 1
            run = _latest_successful_run(args.repo, args.workflow, token)
            run_id = int(run["id"])
            print(f"RUN: {run_id} {run.get('head_sha')} {run.get('html_url')}")
            artifact = _find_artifact(args.repo, run_id, args.artifact, token)
            zip_path = args.output.with_suffix(".zip")
            _download(str(artifact["archive_download_url"]), zip_path, token)
        _extract(zip_path, args.output)
        print(f"EXTRACTED: {args.output}")
        if args.install:
            _install(args.output, args.runtime_plugin)
        return 0
    except urllib.error.HTTPError as exc:
        print(f"FAIL: GitHub API returned HTTP {exc.code}: {exc.reason}")
        return 1
    except Exception as exc:
        print(f"FAIL: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
