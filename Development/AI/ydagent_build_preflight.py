#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
YDTRIGGER_PROJECT = ROOT / "Development" / "Plugin" / "WE" / "YDTrigger" / "YDTrigger.vcxproj"
YDWE_TEST_PROJECT = ROOT / "Development" / "Test" / "YDWE_Test.vcxproj"


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


def main() -> int:
    parser = argparse.ArgumentParser(description="Preflight native build requirements for YDWE Agent validation")
    parser.add_argument("--build-ydtrigger", action="store_true", help="build YDTrigger.vcxproj after v143 is found")
    parser.add_argument("--build-tests", action="store_true", help="build YDWE_Test.vcxproj after v143 is found")
    args = parser.parse_args()

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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
