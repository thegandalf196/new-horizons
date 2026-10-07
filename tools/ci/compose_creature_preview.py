#!/usr/bin/env python3
"""Mechanically assemble a private, detached creature-preview resource tree."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import linux_playable_snapshot as snapshots


def compose(client, resources, overlays, destination):
    if destination.exists() or destination.is_symlink():
        raise ValueError("Preview destination must not already exist")
    files = snapshots.source_payload(client, resources)
    for overlay in overlays:
        additions = snapshots.checked_tree_files(overlay / "Mods/new-horizons", "Mods/new-horizons")
        files.update(additions)
    snapshots.write_payload(files, destination)
    return len(files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--overlay", type=Path, action="append", required=True)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    print(f"Assembled {compose(args.client, args.resources, args.overlay, args.destination)} private payload files")


if __name__ == "__main__":
    main()
