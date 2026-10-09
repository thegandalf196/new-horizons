#!/usr/bin/env python3
"""Copy Soul Chain raster sizes into runtime Images and record their hashes."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil

from PIL import Image


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--staging-root", type=Path, required=True, help="External authoring mirror; never installs shipping art")
    args = parser.parse_args()
    repository_path = Path(__file__).resolve().parents[4]
    staging = args.staging_root.resolve()
    if staging == repository_path or repository_path in staging.parents:
        parser.error("Authoring staging must be outside the checkout")
    source_root = staging / "assets/new-horizons/art-source/soul-chain-v1"
    repo_root = staging
    image_dir = repo_root / "Mods/new-horizons/Images"
    exports = source_root / "exports/soul-chain"
    sizes = (44, 32, 30)
    outputs = {f"NH_spell_soul_chain_{size}.png": exports / f"soul-chain-{size}.png" for size in sizes}
    descriptor_name = "NH_spell_soul_chain.json"
    targets = [image_dir / name for name in outputs]
    targets.append(image_dir / descriptor_name)
    if any(path.exists() for path in targets):
        existing = next(path for path in targets if path.exists())
        raise FileExistsError(existing)

    for size, target in zip(sizes, targets[: len(sizes)]):
        with Image.open(outputs[target.name]) as icon:
            if icon.size != (size, size):
                raise ValueError(f"Unexpected dimensions for {target.name}: {icon.size}")
        shutil.copyfile(outputs[target.name], target)

    descriptor = {
        "schema_version": 1,
        "status": "Provisional",
        "approval": "No in-game visual review or final-art approval recorded.",
        "spell_id": "new-horizons:soulChain",
        "descriptor_role": "Art handoff metadata only; this file is not referenced by spell configuration.",
        "icons": {
            "spellbook": {"file": "NH_spell_soul_chain_44.png", "dimensions": [44, 44]},
            "scroll": {"file": "NH_spell_soul_chain_44.png", "dimensions": [44, 44]},
            "scenario_bonus": {"file": "NH_spell_soul_chain_32.png", "dimensions": [32, 32]},
            "battle_effect": {"file": "NH_spell_soul_chain_30.png", "dimensions": [30, 30]},
            "immunity": {"file": "NH_spell_soul_chain_30.png", "dimensions": [30, 30]},
        },
        "source": "assets/new-horizons/art-source/soul-chain-v1/",
    }
    (image_dir / descriptor_name).write_text(json.dumps(descriptor, indent=2) + "\n", encoding="utf-8")

    files = {str(Path("Mods/new-horizons/Images") / name): sha256(image_dir / name) for name in outputs}
    files[str(Path("Mods/new-horizons/Images") / descriptor_name)] = sha256(image_dir / descriptor_name)
    runtime_manifest = {
        "method": "homm3-art high-resolution master, deterministic LANCZOS reduction",
        "status": "provisional; pending in-game rendering and user approval",
        "assets": ["NH_spell_soul_chain"],
        "files": files,
    }
    (source_root / "runtime-manifest.json").write_text(json.dumps(runtime_manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
