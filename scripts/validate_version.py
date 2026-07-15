#!/usr/bin/env python3

import argparse
import json
import re
import sys
from pathlib import Path

SEMVER_PATTERN = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-[0-9A-Za-z.-]+)?$")


def fail(message: str) -> None:
    print(f"version validation failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def read_properties_version(path: Path) -> str:
    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    version = values.get("version", "")
    if not version:
        fail(f"{path} does not define version")
    return version


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tag", default="", help="Optional release tag, for example v0.1.0")
    parser.add_argument("--root", default=".", help="Repository root")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    properties_version = read_properties_version(root / "library.properties")
    json_version = json.loads((root / "library.json").read_text(encoding="utf-8")).get("version", "")

    if properties_version != json_version:
        fail(
            "library.properties and library.json disagree: "
            f"{properties_version!r} != {json_version!r}"
        )
    if not SEMVER_PATTERN.fullmatch(properties_version):
        fail(f"manifest version is not valid semantic versioning: {properties_version!r}")

    readme = (root / "README.md").read_text(encoding="utf-8")
    expected_status = f"| Status | Early-stage `{properties_version}` |"
    if expected_status not in readme:
        fail(f"README compatibility table does not contain {expected_status!r}")

    if args.tag:
        if not args.tag.startswith("v"):
            fail(f"release tag must start with v: {args.tag!r}")
        tag_version = args.tag[1:]
        if tag_version != properties_version:
            fail(
                f"tag {args.tag!r} does not match manifest version {properties_version!r}"
            )

    print(f"version validation passed: {properties_version}")


if __name__ == "__main__":
    main()
