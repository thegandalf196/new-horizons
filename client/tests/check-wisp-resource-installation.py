#!/usr/bin/env python3
"""Check the Wisp handoff's PNG-sequence resources use the mod's real mounts."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MOD = ROOT / "Mods" / "new-horizons"
SPRITES = MOD / "Content" / "sprites"
IMAGES = MOD / "Images"


def load_descriptor(name):
	return json.loads((SPRITES / name).read_text(encoding="utf-8"))


def check_sequences(name, *, overlays=False, expected_groups=None):
	descriptor = load_descriptor(name)
	sequences = descriptor["sequences"]
	groups = [sequence["group"] for sequence in sequences]
	assert len(groups) == len(set(groups)), f"duplicate groups in {name}"
	if expected_groups is not None:
		assert len(groups) == expected_groups, f"unexpected group count in {name}: {len(groups)}"

	basepath = descriptor.get("basepath", "")
	for sequence in sequences:
		if overlays:
			assert sequence.get("generateOverlay") == 1, f"selection overlay missing in {name} group {sequence['group']}"
		for frame in sequence["frames"]:
			frame_path = IMAGES / basepath / frame
			assert frame_path.is_file(), f"missing {name} frame: {frame_path.relative_to(ROOT)}"


def main() -> None:
	filesystem = json.loads((MOD / "mod.json").read_text(encoding="utf-8"))["filesystem"]
	assert filesystem["SPRITES/"][0]["path"] == "/Images"
	assert filesystem[""][0]["path"] == "/Content"

	check_sequences("Wisp.json", overlays=True, expected_groups=32)
	check_sequences("WispUpgrade.json", overlays=True, expected_groups=32)
	check_sequences("WispMap.json")
	check_sequences("WispUpgradeMap.json")

	for name, expected_basepath in (
		("WispProjectile.json", "WispProjectile/"),
		("WispUpgradeProjectile.json", "WispUpgradeProjectile/"),
	):
		descriptor = load_descriptor(name)
		assert descriptor["basepath"] == expected_basepath, f"unexpected basepath in {name}"
		assert [sequence["group"] for sequence in descriptor["sequences"]] == [0], f"unexpected projectile groups in {name}"
		assert len(descriptor["sequences"][0]["frames"]) == 9, f"unexpected directional frame count in {name}"
		check_sequences(name)

	for directory in ("WispProjectile", "WispUpgradeProjectile"):
		for phase in range(4):
			assert (IMAGES / directory / f"bolt-phase-{phase:02}.png").is_file(), f"missing retained projectile phase {phase} in {directory}"
			for angle in range(9):
				name = f"bolt-phase-{phase:02}-angle-{angle:02}.png"
				assert (IMAGES / directory / name).is_file(), f"missing retained projectile phase frame: {directory}/{name}"

	for path in (
		"Wisp/icons/wisp-icon-32.png",
		"Wisp/icons/wisp-icon-58x64.png",
		"WispUpgrade/icons/icon-32x32.png",
		"WispUpgrade/icons/icon-58x64.png",
	):
		assert (IMAGES / path).is_file(), f"missing creature icon: {path}"

	print("Wisp resource paths, frame references and battle overlays are valid.")


if __name__ == "__main__":
	main()
