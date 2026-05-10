#!/usr/bin/env python3
from __future__ import annotations

import argparse
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


def _extract(zip_path: Path, output_dir: Path) -> None:
    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path) as archive:
        archive.extractall(output_dir)


def _install(output_dir: Path, runtime_plugin: Path) -> None:
    source = output_dir / "Development" / "Build" / "bin" / "Debug" / "plugin" / "YDTrigger.dll"
    if not source.exists():
        source = output_dir / "YDTrigger.dll"
    if not source.exists():
        raise RuntimeError(f"YDTrigger.dll not found in extracted artifact: {output_dir}")

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
    parser = argparse.ArgumentParser(description="Download the latest successful YDWE Agent native CI artifact")
    parser.add_argument("--repo", default=DEFAULT_REPO, help=f"GitHub repo, owner/name (default: {DEFAULT_REPO})")
    parser.add_argument("--workflow", default=DEFAULT_WORKFLOW, help=f"workflow file name (default: {DEFAULT_WORKFLOW})")
    parser.add_argument("--artifact", default=DEFAULT_ARTIFACT, help=f"artifact name (default: {DEFAULT_ARTIFACT})")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT, help=f"extract output dir (default: {DEFAULT_OUTPUT})")
    parser.add_argument("--install", action="store_true", help="install YDTrigger.dll into Build/publish/Debug/plugin after download")
    parser.add_argument("--runtime-plugin", type=Path, default=DEFAULT_RUNTIME_PLUGIN, help=f"runtime plugin dir (default: {DEFAULT_RUNTIME_PLUGIN})")
    args = parser.parse_args()

    token = _token()
    if not token:
        print("FAIL: set GH_TOKEN or GITHUB_TOKEN with Actions artifact read access")
        return 1

    try:
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
