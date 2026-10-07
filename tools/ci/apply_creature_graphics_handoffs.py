#!/usr/bin/env python3
"""Merge disjoint graphics-only handoffs into a detached private payload."""
import argparse
import copy
import json
from pathlib import Path
import re

BUILD_ROOT = Path(__file__).resolve().parents[2] / "build"


def checked_target(payload):
    payload = payload.absolute()
    for parent in [payload, *payload.parents]:
        if parent.is_symlink():
            raise ValueError("Payload must not have symlink ancestry")
    try:
        payload.resolve(strict=True).relative_to(BUILD_ROOT.resolve(strict=True))
    except (ValueError, FileNotFoundError) as error:
        raise ValueError("Payload must be a detached directory inside repository build/") from error
    if payload.resolve() == BUILD_ROOT.resolve():
        raise ValueError("Do not patch the build root")
    target = payload / "Mods/new-horizons/Content/config/creatures/tower.json"
    for parent in [target, *target.parents]:
        if parent.is_symlink():
            raise ValueError("Configuration must not have symlink ancestry")
    if not target.is_file():
        raise ValueError("Expected a detached payload Tower configuration")
    return target


def merge_graphics(baseline, patches):
    result = copy.deepcopy(baseline)
    seen = set()
    for patch in patches:
        if set(patch) != {"creatures"} or not isinstance(patch["creatures"], dict):
            raise ValueError("Expected a graphics-only creatures patch")
        for creature, change in patch["creatures"].items():
            if creature in seen or creature not in result:
                raise ValueError(f"Duplicate or unknown creature: {creature}")
            seen.add(creature)
            if set(change) != {"set", "remove"} or not isinstance(change["set"], dict):
                raise ValueError("Expected set/remove graphics fields")
            if not isinstance(change["remove"], list) or not all(isinstance(x, str) for x in change["remove"]):
                raise ValueError("Invalid graphics removals")
            graphics = result[creature].setdefault("graphics", {})
            for field in change["remove"]:
                graphics.pop(field, None)
            def merge(target, source):
                for key, value in source.items():
                    if isinstance(value, dict) and isinstance(target.get(key), dict):
                        merge(target[key], value)
                    else:
                        target[key] = copy.deepcopy(value)
            merge(graphics, change["set"])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--payload", type=Path, required=True)
    parser.add_argument("--patch", type=Path, action="append", required=True)
    args = parser.parse_args()
    target = checked_target(args.payload)
    baseline = json.loads(re.sub(r"(?m)^\s*//[^\n]*(?:\n|$)", "\n", target.read_text()))
    patches = [json.loads(path.read_text()) for path in args.patch]
    result = merge_graphics(baseline, patches)
    target.write_text(json.dumps(result, indent="\t") + "\n")
    print(f"Applied {sum(len(p['creatures']) for p in patches)} private graphics bindings")


if __name__ == "__main__":
    main()
