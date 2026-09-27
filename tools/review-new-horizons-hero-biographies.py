#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build a local-only biography comparison from purchaser-supplied H3 data."""

import argparse
import csv
import json
from pathlib import Path
import re
import struct
import zlib


ROOT = Path(__file__).resolve().parents[1]
FACTIONS = (
    "castle", "rampart", "tower", "inferno", "necropolis", "dungeon",
    "stronghold", "fortress", "conflux",
)
ARCHIVES = ("H3ab_bmp.lod", "h3abp_bm.lod", "H3bitmap.lod", "H3pbitma.lod")


def read_lod_entry(archive: Path, requested_name: str) -> bytes | None:
    data = archive.read_bytes()
    if len(data) < 0x5C:
        raise ValueError(f"invalid LOD header: {archive}")
    entry_count = struct.unpack_from("<I", data, 8)[0]
    table_end = 0x5C + entry_count * 32
    if table_end > len(data):
        raise ValueError(f"truncated LOD directory: {archive}")
    for index in range(entry_count):
        offset = 0x5C + index * 32
        raw_name = data[offset:offset + 16].split(b"\0", 1)[0]
        name = raw_name.decode("ascii", errors="strict")
        if name.casefold() != requested_name.casefold():
            continue
        payload_offset, full_size, _, compressed_size = struct.unpack_from("<IIII", data, offset + 16)
        stored_size = compressed_size or full_size
        payload_end = payload_offset + stored_size
        if payload_end > len(data):
            raise ValueError(f"truncated {name} payload in {archive}")
        payload = data[payload_offset:payload_end]
        if compressed_size:
            payload = zlib.decompress(payload)
        if len(payload) != full_size:
            raise ValueError(f"wrong expanded size for {name} in {archive}")
        return payload
    return None


def installed_biographies(data_root: Path) -> list[str]:
    payload = None
    selected = None
    for archive_name in ARCHIVES:
        archive = data_root / archive_name
        if not archive.is_file():
            continue
        candidate = read_lod_entry(archive, "HEROBIOS.TXT")
        if candidate is not None:
            # Match config/filesystem.json: later archives override earlier ones.
            payload = candidate
            selected = archive
    if payload is None:
        raise ValueError(f"HEROBIOS.TXT was not found under {data_root}")
    rows = list(csv.reader(payload.decode("cp1252").splitlines(), delimiter="\t", quotechar='"'))
    biographies = [row[0] if row else "" for row in rows]
    if len(biographies) < 144:
        raise ValueError(f"{selected} contains only {len(biographies)} hero biographies")
    return biographies


def hero_inventory() -> list[tuple[str, str, int]]:
    result = []
    for faction in FACTIONS:
        heroes = json.loads((ROOT / "config/heroes" / f"{faction}.json").read_text(encoding="utf-8"))
        for hero_id, definition in heroes.items():
            result.append((faction, hero_id, definition["index"]))
    result.sort(key=lambda item: item[2])
    if len(result) != 144 or [entry[2] for entry in result] != list(range(144)):
        raise ValueError("standard faction hero indices must cover 0..143 exactly")
    return result


def candidate_biographies() -> dict[tuple[str, str], str | None]:
    result = {}
    for faction in FACTIONS:
        path = ROOT / "config/newHorizonsHeroBiographies" / f"{faction}.json"
        candidates = json.loads(path.read_text(encoding="utf-8"))
        expected = {hero_id for current, hero_id, _ in hero_inventory() if current == faction}
        if set(candidates) != expected:
            raise ValueError(f"candidate IDs do not match core {faction} heroes")
        result.update({(faction, hero_id): text for hero_id, text in candidates.items()})
    return result


def workbook_biographies(workbook: Path) -> dict[str, list[tuple[str, str]]]:
    result = {faction: [] for faction in FACTIONS}
    current_faction = None
    current_hero = None
    for line in workbook.read_text(encoding="utf-8").splitlines():
        if line.startswith("# "):
            candidate = line[2:].strip().casefold()
            current_faction = candidate if candidate in result else None
            current_hero = None
        elif current_faction and line.startswith("### "):
            current_hero = line[4:].strip()
        elif current_faction and current_hero and line.startswith("**Biography:**"):
            result[current_faction].append((current_hero, line[len("**Biography:**"):].strip()))
            current_hero = None
    if any(len(entries) != 16 for entries in result.values()):
        counts = {faction: len(entries) for faction, entries in result.items()}
        raise ValueError(f"workbook biography counts are incomplete: {counts}")
    return result


def escape_markdown(text: str | None) -> str:
    if text is None:
        return "*Inherit the installed original; no New Horizons override.*"
    return text.replace("\r", " ").replace("\n", " ").strip()


def normalized_hero_name(name: str) -> str:
    return "".join(re.findall(r"[a-z0-9]+", name.casefold()))


def build_report(data_root: Path, workbook: Path) -> str:
    originals = installed_biographies(data_root)
    inventory = hero_inventory()
    candidates = candidate_biographies()
    workbook_entries = workbook_biographies(workbook)
    inventory_by_faction = {
        faction: [(hero_id, index) for current, hero_id, index in inventory if current == faction]
        for faction in FACTIONS
    }
    lines = [
        "# New Horizons hero biography review",
        "",
        "Generated locally from purchaser-supplied Heroes III data. Do not commit this report.",
        "Every decision begins as **Pending**; candidate presence is not approval or runtime binding.",
        "",
    ]
    for faction in FACTIONS:
        lines.extend((f"## {faction.title()}", ""))
        drafts = {
            normalized_hero_name(display_name): (display_name, workbook_text)
            for display_name, workbook_text in workbook_entries[faction]
        }
        if len(drafts) != 16:
            raise ValueError(f"workbook contains duplicate {faction} hero names")
        for hero_id, index in inventory_by_faction[faction]:
            hero_key = normalized_hero_name(hero_id)
            if hero_key not in drafts:
                raise ValueError(f"workbook has no {faction} biography for core:{hero_id}")
            display_name, workbook_text = drafts[hero_key]
            lines.extend((
                f"### {display_name} (`core:{hero_id}`)",
                "",
                "**Decision:** Pending",
                "",
                f"**Installed original:** {escape_markdown(originals[index])}",
                "",
                f"**Workbook draft:** {escape_markdown(workbook_text)}",
                "",
                f"**Candidate:** {escape_markdown(candidates[(faction, hero_id)])}",
                "",
                "**Review:** lore fidelity ☐ · distinct voice ☐ · material improvement ☐ · no unsupported additions ☐",
                "",
            ))
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", required=True, type=Path,
                        help="Heroes III Data directory containing H3bitmap.lod")
    parser.add_argument("--workbook", required=True, type=Path,
                        help="Locally supplied New_Horizons_Hero_Redesign_Workbook.md")
    parser.add_argument("--output", type=Path,
                        default=ROOT / "build/new-horizons-biography-review.md")
    args = parser.parse_args()
    output = args.output.resolve()
    try:
        output.relative_to((ROOT / "build").resolve())
    except ValueError:
        parser.error("review output must remain under build/")
    if output.exists() or output.is_symlink():
        parser.error("refusing existing output or symlink")
    report = build_report(args.data_root.resolve(), args.workbook.resolve())
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(report, encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
