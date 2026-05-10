#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
YDTRIGGER_PROJECT = ROOT / "Development" / "Plugin" / "WE" / "YDTrigger" / "YDTrigger.vcxproj"
YDWE_TEST_PROJECT = ROOT / "Development" / "Test" / "YDWE_Test.vcxproj"
RUNTIME_YDTRIGGER = ROOT / "Build" / "publish" / "Debug" / "plugin" / "YDTrigger.dll"
REQUIRED_YDTRIGGER_EXPORTS = {
    "ydt_get_eca_active",
    "ydt_add_eca",
    "ydt_remove_eca",
    "ydt_read_object_file",
    "ydt_write_object_file",
}


class PreflightError(RuntimeError):
    pass


def _candidate_msbuilds() -> list[Path]:
    candidates: list[Path] = []
    for part in os.environ.get("PATH", "").split(os.pathsep):
        path = Path(part) / "MSBuild.exe"
        if path.exists():
            candidates.append(path)

    roots = [
        Path(os.environ.get("ProgramFiles", r"C:\Program Files")),
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")),
    ]
    patterns = [
        "Microsoft Visual Studio/*/*/MSBuild/Current/Bin/MSBuild.exe",
        "Microsoft Visual Studio/*/*/MSBuild/*/Bin/MSBuild.exe",
        "MSBuild/*/Bin/MSBuild.exe",
    ]
    for root in roots:
        for pattern in patterns:
            candidates.extend(root.glob(pattern))

    seen: set[str] = set()
    result: list[Path] = []
    for path in candidates:
        key = str(path.resolve()).lower()
        if key not in seen:
            seen.add(key)
            result.append(path.resolve())
    return result


def _has_v143(msbuild: Path) -> bool:
    root = msbuild
    for parent in [root, *root.parents]:
        toolset = parent / "Microsoft.Cpp" / "v4.0" / "V143" / "Microsoft.Cpp.Platform.targets"
        if toolset.exists():
            return True
    roots = [
        Path(os.environ.get("ProgramFiles", r"C:\Program Files")),
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")),
    ]
    for root in roots:
        if any(root.glob("Microsoft Visual Studio/*/*/MSBuild/Microsoft/VC/v170/Microsoft.Cpp.Default.props")):
            return True
    return False


def _run(msbuild: Path, project: Path) -> int:
    cmd = [
        str(msbuild),
        str(project),
        "/t:Build",
        "/p:Configuration=Debug",
        "/p:Platform=Win32",
    ]
    print("RUN: " + " ".join(cmd))
    return subprocess.call(cmd, cwd=ROOT)


def _read_c_string(data: bytes, offset: int) -> str:
    end = data.find(b"\0", offset)
    if end < 0:
        end = len(data)
    return data[offset:end].decode("ascii", errors="replace")


def _read_pe_exports(dll_path: Path) -> set[str]:
    data = dll_path.read_bytes()
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise PreflightError(f"not a PE file: {dll_path}")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise PreflightError(f"invalid PE signature: {dll_path}")

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
        raise PreflightError(f"unsupported PE optional header magic: 0x{magic:x}")

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
                    raise PreflightError(f"RVA points outside raw section: 0x{rva:x}")
                return offset
        raise PreflightError(f"RVA not found in PE sections: 0x{rva:x}")

    export_offset = rva_to_offset(export_rva)
    name_count = struct.unpack_from("<I", data, export_offset + 24)[0]
    names_rva = struct.unpack_from("<I", data, export_offset + 32)[0]
    names_offset = rva_to_offset(names_rva)
    exports: set[str] = set()
    for index in range(name_count):
        name_rva = struct.unpack_from("<I", data, names_offset + index * 4)[0]
        exports.add(_read_c_string(data, rva_to_offset(name_rva)))
    return exports


def _check_runtime_exports(dll_path: Path) -> int:
    if not dll_path.exists():
        print(f"FAIL: runtime YDTrigger.dll not found: {dll_path}")
        return 1
    try:
        exports = _read_pe_exports(dll_path)
    except PreflightError as exc:
        print(f"FAIL: {exc}")
        return 1
    missing = sorted(REQUIRED_YDTRIGGER_EXPORTS - exports)
    if missing:
        print(f"FAIL: runtime YDTrigger.dll missing required Agent exports: {', '.join(missing)}")
        return 1
    print(f"PASS: runtime YDTrigger.dll exports ready: {dll_path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Preflight native build requirements for YDWE Agent validation")
    parser.add_argument("--build-ydtrigger", action="store_true", help="build YDTrigger.vcxproj after v143 is found")
    parser.add_argument("--build-tests", action="store_true", help="build YDWE_Test.vcxproj after v143 is found")
    parser.add_argument(
        "--check-runtime-exports",
        action="store_true",
        help="verify the local Debug runtime YDTrigger.dll contains the required refactor Agent exports",
    )
    args = parser.parse_args()

    needs_native_build = args.build_ydtrigger or args.build_tests

    if needs_native_build:
        msbuilds = _candidate_msbuilds()
        if not msbuilds:
            print("FAIL: MSBuild.exe not found")
            return 1

        print("MSBuild candidates:")
        for path in msbuilds:
            print(f"  {path}")

        msbuild = next((path for path in msbuilds if _has_v143(path)), None)
        if msbuild is None:
            print("FAIL: v143 toolset not found; install Visual Studio 2022 Build Tools with C++ workload")
            return 1

        print(f"PASS: v143 toolset available via {msbuild}")

        if args.build_ydtrigger:
            code = _run(msbuild, YDTRIGGER_PROJECT)
            if code != 0:
                return code
        if args.build_tests:
            code = _run(msbuild, YDWE_TEST_PROJECT)
            if code != 0:
                return code
    if args.check_runtime_exports:
        code = _check_runtime_exports(RUNTIME_YDTRIGGER)
        if code != 0:
            return code
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
