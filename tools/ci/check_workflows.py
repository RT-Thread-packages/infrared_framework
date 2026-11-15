#!/usr/bin/env python3
"""Static consistency checks for this repository's GitHub Actions workflows."""

import pathlib
import re
import sys

import yaml

from check_matrix import PROFILES, RTTHREAD_BUILD_MATRIX

ROOT = pathlib.Path(__file__).resolve().parents[2]
WF_DIR = ROOT / ".github/workflows"
ACTION = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+@[0-9a-f]{40}$")


def require(ok, message):
    if not ok:
        raise AssertionError(message)


def check_file(path):
    text = path.read_text(encoding="utf-8")
    doc = yaml.safe_load(text)
    require(isinstance(doc, dict) and isinstance(doc.get("on"), dict),
            f"{path.name}: missing workflow trigger")
    require(doc.get("permissions") == {"contents": "read"},
            f"{path.name}: unexpected workflow-level permissions")
    jobs = doc.get("jobs") or {}
    require(isinstance(jobs, dict) and bool(jobs), f"{path.name}: no jobs")
    uses = list(re.finditer(r"^\s*uses:\s*(\S+)(?:\s*#\s*(\S+))?\s*$",
                            text, re.MULTILINE))
    require(bool(uses), f"{path.name}: workflow has no pinned Actions")
    for match in uses:
        require(bool(ACTION.fullmatch(match.group(1))),
                f"{path.name}: Action not pinned to full SHA: {match.group(1)}")
        require(bool(match.group(2)) and match.group(2).startswith("v"),
                f"{path.name}: Action pin must have version comment: {match.group(1)}")
    require("pull_request_target" not in doc["on"],
            f"{path.name}: privileged pull_request_target event is disallowed")
    require("continue-on-error" not in text, "CI gates may not be ignored")
    return doc


def main():
    ci = check_file(WF_DIR / "ci.yml")
    pages = check_file(WF_DIR / "pages.yml")
    build = ci["jobs"].get("stm32f407-build") or {}
    configured = build.get("strategy", {}).get("matrix", {}).get("include", [])
    require(configured == RTTHREAD_BUILD_MATRIX,
            "CI RT-Thread/profile matrix is different from its supported build cases")
    require(build.get("needs") == "config-matrix", "BSP builds must depend on config validation")
    require(build.get("env", {}).get("RTTHREAD_REF") == "${{ matrix.rtthread }}",
            "build job must check out each declared RT-Thread version")
    for job in ("cppcheck", "documentation", "config-matrix", "stm32f407-build"):
        require(job in ci["jobs"], f"missing CI quality gate: {job}")
    require("pull_request" in ci["on"], "CI must run for pull requests")
    require("push" in pages["on"] and not pages["on"].get("push"),
            "Pages must build on pushes to all branches")
    require("github.ref == 'refs/heads/master'" not in
            (WF_DIR / "pages.yml").read_text(encoding="utf-8"),
            "Pages deployment must not be restricted to master")
    require(pages.get("concurrency", {}).get("group") == "github-pages",
            "Pages must have a single deployment concurrency group")
    deploy = pages["jobs"].get("deploy") or {}
    require(deploy.get("needs") == "build", "Pages deployment must follow site build")
    require(deploy.get("permissions") == {"pages": "write", "id-token": "write"},
            "Pages deploy job must use least-privilege publication permissions")
    require(deploy.get("environment", {}).get("name") == "github-pages",
            "Pages must publish through github-pages environment")
    require((ROOT / "Doxyfile").is_file() and (ROOT / "mkdocs.yml").is_file(),
            "documentation build configuration is missing")
    print(f"PASS workflow structure/action pins: 2 workflows, {len(configured)} build profiles")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, OSError, yaml.YAMLError) as exc:
        print(f"INFRARED_WORKFLOW_FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
