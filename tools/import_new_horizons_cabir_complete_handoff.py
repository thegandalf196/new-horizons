#!/usr/bin/env python3
"""Build a private, pinned full-state Cabir handoff overlay.

The refreshed handoff supplies every production-required battle group. This
tool copies those native RGBA frames without changing pixels, and reuses only
the previously retained map, icon, and projectile resources. It emits a
graphics-only patch for root integration; it never edits the live module.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
from io import BytesIO
import json
import os
from pathlib import Path, PurePosixPath
import sys
from typing import Any

from PIL import Image, UnidentifiedImageError

import import_new_horizons_cabir_handoff as prior_import


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PACKAGE = ROOT / "output/handoff-20261007-refreshed/cabir"
DEFAULT_PRIOR_PACKAGE = ROOT / "output/handoff-20261007/cabir"
DEFAULT_OUTPUT = ROOT / "build/nh-cabir-complete-handoff-20261007"

EXPECTED_PACKAGE_MANIFEST_SHA256 = "e07acb2dfe61d430f118b39b6af96416a8946442b79e17c6a76d35672e8f1eeb"
EXPECTED_EXPORT_MANIFEST_SHA256 = "1de67e687cf1ddedb122c774bdf059a87bfcd9a9b9987bdeebdd7ec96532f67d"
EXPECTED_PRIOR_HANDOFF_MANIFEST_SHA256 = prior_import.HANDOFF_MANIFEST_SHA256
EXPECTED_PACKAGE_FILE_COUNT = 422

MOD_ROOT = "Mods/new-horizons"
IMAGE_PREFIX = f"{MOD_ROOT}/Images"
SPRITE_PREFIX = f"{MOD_ROOT}/Content/sprites"
PATCH_NAME = "GRAPHICS_PATCH.json"
MANIFEST_NAME = "CABIR_COMPLETE_HANDOFF_MANIFEST.json"
BATTLE_CANVAS = (450, 400)

FRAME_ANGLES = [90, 72, 45, 27, 0, -27, -45, -72, -90]
MOUTH_CANVAS = {
    "ranged": [230, 223],
    "shoot-up": [223, 217],
    "shoot-down": [230, 237],
}
PROJECTILE_ANGLE_FRAME = {"shoot-up": 2, "ranged": 4, "shoot-down": 6}
PROJECTILE_OFFSET = {
    "upperX": 26,
    "upperY": -48,
    "middleX": 33,
    "middleY": -42,
    "lowerX": 33,
    "lowerY": -28,
}
ENGINE_GROUPS = (
    (0, "MOVING"),
    (1, "MOUSEON"),
    (2, "HOLDING"),
    (3, "HITTED"),
    (4, "DEFENCE"),
    (5, "DEATH"),
    (6, "DEATH_RANGED"),
    (7, "TURN_L"),
    (8, "TURN_R"),
    (11, "ATTACK_UP"),
    (12, "ATTACK_FRONT"),
    (13, "ATTACK_DOWN"),
    (14, "SHOOT_UP"),
    (15, "SHOOT_FRONT"),
    (16, "SHOOT_DOWN"),
    (17, "SPECIAL_UP"),
    (18, "SPECIAL_FRONT"),
    (19, "SPECIAL_DOWN"),
    (20, "MOVE_START"),
    (21, "MOVE_END"),
    (22, "DEAD"),
    (23, "DEAD_RANGED"),
    (24, "RESURRECTION"),
    (25, "FROZEN"),
    (30, "CAST_UP"),
    (31, "CAST_FRONT"),
    (32, "CAST_DOWN"),
    (40, "GROUP_ATTACK_UP"),
    (41, "GROUP_ATTACK_FRONT"),
    (42, "GROUP_ATTACK_DOWN"),
    (50, "TELEPORT_START"),
    (51, "TELEPORT_END"),
)


@dataclass(frozen=True)
class StateSpec:
	group: int
	name: str
	source_folder: str
	count: int


COMMON_STATES = (
	StateSpec(0, "MOVING", "walk", 7),
	StateSpec(2, "HOLDING", "idle", 1),
	StateSpec(3, "HITTED", "hit", 6),
	StateSpec(4, "DEFENCE", "defend", 6),
	StateSpec(5, "DEATH", "death", 8),
	StateSpec(7, "TURN_L", "turn-out", 2),
	StateSpec(8, "TURN_R", "turn-in", 2),
	StateSpec(11, "ATTACK_UP", "melee-up", 6),
	StateSpec(12, "ATTACK_FRONT", "melee", 6),
	StateSpec(13, "ATTACK_DOWN", "melee-down", 6),
	StateSpec(14, "SHOOT_UP", "shoot-up", 6),
	StateSpec(15, "SHOOT_FRONT", "ranged", 6),
	StateSpec(16, "SHOOT_DOWN", "shoot-down", 6),
	StateSpec(20, "MOVE_START", "move-start", 4),
	StateSpec(21, "MOVE_END", "move-end", 4),
	StateSpec(22, "DEAD", "dead", 1),
)
FORM_SPECS = {
	"cabir": {
		"creatureId": "core:gremlin",
		"sourceForm": "cabir",
		"battleDescriptor": "NH_CabirCompleteHandoff.def",
		"battleBase": "cabir-complete-handoff/cabir/battle/",
		"mapDescriptor": "NH_CabirHandoffMap.def",
		"mapAttack": "NH_CabirHandoffMap:0:0",
		"iconLarge": "cabir-handoff/cabir/icons/NH_cabir_handoff_icon_large.png",
		"iconSmall": "cabir-handoff/cabir/icons/NH_cabir_handoff_icon_small.png",
		"projectile": "NH_CabirHandoffFireball.def",
		"runtimeIcons": {
			"large": "NH_cabir_handoff_icon_large.png",
			"small": "NH_cabir_handoff_icon_small.png",
		},
		"states": COMMON_STATES,
	},
	"cabir-master": {
		"creatureId": "core:masterGremlin",
		"sourceForm": "cabir-master",
		"battleDescriptor": "NH_CabirMasterCompleteHandoff.def",
		"battleBase": "cabir-complete-handoff/cabir-master/battle/",
		"mapDescriptor": "NH_CabirMasterHandoffMap.def",
		"mapAttack": "NH_CabirMasterHandoffMap:0:0",
		"iconLarge": "cabir-handoff/cabir-master/icons/NH_cabir_master_handoff_icon_large.png",
		"iconSmall": "cabir-handoff/cabir-master/icons/NH_cabir_master_handoff_icon_small.png",
		"projectile": "NH_CabirHandoffFireball.def",
		"runtimeIcons": {
			"large": "NH_cabir_master_handoff_icon_large.png",
			"small": "NH_cabir_master_handoff_icon_small.png",
		},
		"states": COMMON_STATES + (StateSpec(18, "SPECIAL_FRONT", "repair", 6),),
	},
}

EXPECTED_EXPORT_STATE_KEYS = {
	"cabir/walk", "cabir/melee", "cabir/ranged", "cabir/idle", "cabir/melee-up",
	"cabir/melee-down", "cabir/shoot-up", "cabir/shoot-down", "cabir/hit",
	"cabir/defend", "cabir/death", "cabir/dead", "cabir/move-start", "cabir/move-end",
	"cabir/turn-out", "cabir/turn-in", "cabir/turn-right-to-left", "cabir/turn-left-to-right",
	"cabir-master/walk", "cabir-master/melee", "cabir-master/ranged", "cabir-master/idle",
	"cabir-master/melee-up", "cabir-master/melee-down", "cabir-master/shoot-up",
	"cabir-master/shoot-down", "cabir-master/hit", "cabir-master/defend", "cabir-master/death",
	"cabir-master/dead", "cabir-master/move-start", "cabir-master/move-end", "cabir-master/turn-out",
	"cabir-master/turn-in", "cabir-master/turn-right-to-left", "cabir-master/turn-left-to-right",
	"cabir-master/repair",
}

OPTIONAL_GROUP_NOTES = {
	1: "MOUSEON is optional; BattleStacksController plays it only when frames exist.",
	6: "DEATH_RANGED is optional; ranged death uses it only when frames exist, otherwise group 5.",
	17: "SPECIAL_UP falls through to SPECIAL_FRONT, then normal attack/shoot choices at call sites.",
	19: "SPECIAL_DOWN falls through to SPECIAL_FRONT, then normal attack/shoot choices at call sites.",
	23: "DEAD_RANGED is synthesized only when group 6 exists; group 6 is absent.",
	24: "CreatureAnimation synthesizes resurrection by reversing group 5.",
	25: "CreatureAnimation synthesizes frozen by duplicating the first group 2 frame.",
	30: "CAST_UP selection falls through special/shoot/attack groups when absent.",
	31: "CAST_FRONT selection falls through special/shoot/attack groups when absent.",
	32: "CAST_DOWN selection falls through special/shoot/attack groups when absent.",
	40: "Multi-attack selection falls through special/front and ordinary attack groups.",
	41: "Multi-attack selection falls through special/front and ordinary attack groups.",
	42: "Multi-attack selection falls through special/front and ordinary attack groups.",
	50: "Teleport start is chosen only when present; supplied MOVE_START remains available.",
	51: "Teleport end is chosen only when present; supplied MOVE_END remains available.",
}


def _sha256(data: bytes) -> str:
	return hashlib.sha256(data).hexdigest()


def _json_bytes(value: Any) -> bytes:
	return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def _safe_relative(path: str) -> PurePosixPath:
	rel = PurePosixPath(path)
	if rel.is_absolute() or ".." in rel.parts or not rel.parts:
		raise ValueError(f"unsafe package path: {path}")
	return rel


def _checked_file(root: Path, relative: str, pins: dict[str, dict[str, Any]]) -> tuple[bytes, str]:
	rel = _safe_relative(relative)
	current = root
	for component in rel.parts:
		current = current / component
		if current.is_symlink():
			raise ValueError(f"package input must not use symlinks: {relative}")
	resolved = current.resolve()
	try:
		resolved.relative_to(root.resolve())
	except ValueError as error:
		raise ValueError(f"package input escaped its root: {relative}") from error
	if not resolved.is_file():
		raise ValueError(f"required package input is missing: {relative}")
	record = pins.get(relative)
	if not isinstance(record, dict):
		raise ValueError(f"package manifest does not pin {relative}")
	data = resolved.read_bytes()
	actual = _sha256(data)
	if actual != record.get("sha256") or len(data) != record.get("bytes"):
		raise ValueError(f"package input hash or length mismatch: {relative}")
	return data, actual


def _parse_object(data: bytes, label: str) -> dict:
	try:
		value = json.loads(data)
	except (UnicodeDecodeError, json.JSONDecodeError) as error:
		raise ValueError(f"{label} is not valid JSON") from error
	if not isinstance(value, dict):
		raise ValueError(f"{label} must be a JSON object")
	return value


def _load_package_manifest(
	package_root: Path,
	*,
	expected_sha256: str | None = EXPECTED_PACKAGE_MANIFEST_SHA256,
	expected_file_count: int | None = EXPECTED_PACKAGE_FILE_COUNT,
) -> tuple[dict[str, dict[str, Any]], str]:
	if package_root.is_symlink() or not package_root.is_dir():
		raise ValueError("refreshed Cabir package must be an existing non-symlink directory")
	package_root = package_root.resolve()
	manifest_path = package_root / "PACKAGE_MANIFEST.json"
	if manifest_path.is_symlink() or not manifest_path.is_file():
		raise ValueError("refreshed Cabir PACKAGE_MANIFEST.json is missing or symlinked")
	data = manifest_path.read_bytes()
	manifest_sha = _sha256(data)
	if expected_sha256 is not None and manifest_sha != expected_sha256:
		raise ValueError("refreshed package manifest identity changed")
	manifest = _parse_object(data, "PACKAGE_MANIFEST.json")
	if manifest.get("status") != "Private complete artwork review candidate":
		raise ValueError("refreshed package is not the complete Cabir artwork review candidate")
	files = manifest.get("files")
	if not isinstance(files, list) or (expected_file_count is not None and len(files) != expected_file_count):
		raise ValueError("refreshed package manifest has an unexpected inventory")
	pins: dict[str, dict[str, Any]] = {}
	for record in files:
		if not isinstance(record, dict):
			raise ValueError("package manifest contains a malformed file record")
		relative = record.get("path")
		if not isinstance(relative, str):
			raise ValueError("package manifest contains a pathless file record")
		_safe_relative(relative)
		sha = record.get("sha256")
		length = record.get("bytes")
		if not isinstance(sha, str) or len(sha) != 64 or not isinstance(length, int) or length < 0:
			raise ValueError(f"package manifest has invalid pin data for {relative}")
		if relative in pins:
			raise ValueError(f"package manifest repeats {relative}")
		pins[relative] = record
	return pins, manifest_sha


def _read_rgba(data: bytes, expected_size: tuple[int, int], label: str) -> Image.Image:
	try:
		with Image.open(BytesIO(data)) as opened:
			if opened.format != "PNG" or opened.mode != "RGBA" or getattr(opened, "is_animated", False):
				raise ValueError(f"{label} must be a static RGBA PNG")
			if opened.size != expected_size:
				raise ValueError(f"{label} expected {expected_size}, got {opened.size}")
			opened.load()
			frame = opened.copy()
	except UnidentifiedImageError as error:
		raise ValueError(f"{label} is not a readable PNG") from error
	except OSError as error:
		raise ValueError(f"{label} PNG is incomplete or unreadable") from error
	alpha = frame.getchannel("A")
	if alpha.getbbox() is None or alpha.getextrema() == (0, 0):
		raise ValueError(f"{label} has no visible pixels")
	return frame


def _expected_engine_states(export_manifest: dict) -> None:
	state_manifest = export_manifest.get("engineStateGroups")
	if not isinstance(state_manifest, dict) or set(state_manifest) != EXPECTED_EXPORT_STATE_KEYS:
		raise ValueError("export manifest engine/review state inventory changed")
	for form, spec in FORM_SPECS.items():
		for state in spec["states"]:
			key = f"{form}/{state.source_folder}"
			item = state_manifest.get(key)
			if not isinstance(item, dict):
				raise ValueError(f"export manifest is missing {key}")
			if item.get("group") != state.group or item.get("count") != state.count or item.get("canvas") != list(BATTLE_CANVAS):
				raise ValueError(f"export manifest registration changed for {key}")
	for form in FORM_SPECS:
		for review_state in ("turn-right-to-left", "turn-left-to-right"):
			item = state_manifest.get(f"{form}/{review_state}")
			if item.get("group") is not None or item.get("count") != 4 or item.get("canvas") != list(BATTLE_CANVAS):
				raise ValueError(f"review-only full-turn sequence changed: {form}/{review_state}")
	if export_manifest.get("turnContract") != "group7 first half; facing flip; group8 locally mirrored second half. Full turn GIFs show world-space result.":
		raise ValueError("turn contract changed; do not bind review GIF timelines as engine groups")
	if export_manifest.get("mouthCanvasXY") != MOUTH_CANVAS:
		raise ValueError("refreshed mouth registration changed")
	if export_manifest.get("projectileAngleChoices") != PROJECTILE_ANGLE_FRAME:
		raise ValueError("refreshed projectile-angle choices changed")


def _load_refreshed_package(
	package_dir: Path,
	*,
	expected_package_sha256: str | None = EXPECTED_PACKAGE_MANIFEST_SHA256,
	expected_export_sha256: str | None = EXPECTED_EXPORT_MANIFEST_SHA256,
	expected_file_count: int | None = EXPECTED_PACKAGE_FILE_COUNT,
) -> tuple[dict[str, bytes], dict[str, str], dict]:
	package_root = Path(package_dir)
	pins, package_manifest_sha = _load_package_manifest(
		package_root,
		expected_sha256=expected_package_sha256,
		expected_file_count=expected_file_count,
	)
	export_manifest_bytes, export_manifest_sha = _checked_file(package_root, "export/manifest.json", pins)
	if expected_export_sha256 is not None and export_manifest_sha != expected_export_sha256:
		raise ValueError("refreshed export manifest identity changed")
	export_manifest = _parse_object(export_manifest_bytes, "export/manifest.json")
	if export_manifest.get("status") != "Complete requested artwork review candidate; aesthetic approval and runtime integration pending":
		raise ValueError("refreshed export is not marked as a review candidate")
	_expected_engine_states(export_manifest)
	verification_bytes, _ = _checked_file(package_root, "verification.json", pins)
	verification = _parse_object(verification_bytes, "verification.json")
	if verification.get("runtimeInstalled") is not False or verification.get("aestheticApproval") != "pending":
		raise ValueError("refreshed package verification must remain a pending private review, not installed/approved")

	assets: dict[str, bytes] = {}
	source_hashes: dict[str, str] = {}
	for form, spec in FORM_SPECS.items():
		for state in spec["states"]:
			for index in range(state.count):
				relative = f"export/animations/{form}/{state.source_folder}/frame-{index:02}.png"
				data, digest = _checked_file(package_root, relative, pins)
				_read_rgba(data, BATTLE_CANVAS, relative)
				assets[relative] = data
				source_hashes[relative] = digest
	return assets, source_hashes, {
		"packageManifestSha256": package_manifest_sha,
		"exportManifestSha256": export_manifest_sha,
		"packageFileCount": len(pins),
		"exportManifest": export_manifest,
		"packagePins": pins,
		"packageRoot": package_root.resolve(),
	}


def _preserved_prior_paths() -> set[str]:
	paths = {
		f"{prior_import.CONTENT_PREFIX}/sprites/NH_CabirHandoffMap.json",
		f"{prior_import.CONTENT_PREFIX}/sprites/NH_CabirMasterHandoffMap.json",
		f"{prior_import.CONTENT_PREFIX}/sprites/{prior_import.PROJECTILE_DESCRIPTOR.replace('.def', '.json')}",
	}
	for form_key, form in prior_import.FORMS.items():
		for index in range(7):
			paths.add(f"{prior_import.IMAGE_PREFIX}/cabir-handoff/{form_key}/map/walk/frame-{index:02}.png")
		for icon_name in form["runtimeIcons"].values():
			paths.add(f"{prior_import.IMAGE_PREFIX}/cabir-handoff/{form_key}/icons/{icon_name}")
	for index in range(9):
		paths.add(f"{prior_import.IMAGE_PREFIX}/cabir-handoff/projectile/frame-{index:02}.png")
	return paths


def _select_preserved_prior_outputs(prior_outputs: dict[str, bytes]) -> dict[str, bytes]:
	expected = _preserved_prior_paths()
	missing = sorted(expected - prior_outputs.keys())
	if missing:
		raise ValueError(f"retained prior Cabir import is missing approved map/icon/projectile assets: {missing[0]}")
	return {relative.removeprefix("overlay/"): prior_outputs[relative] for relative in sorted(expected)}


def _assert_prior_resources_match_refreshed_inputs(
	prior_outputs: dict[str, bytes], package_root: Path, pins: dict[str, dict[str, Any]]
) -> dict[str, str]:
	verified: dict[str, str] = {}
	for form_key, prior_form in prior_import.FORMS.items():
		for index in range(7):
			source_relative = f"approved-inputs/animations/{form_key}/walk/frame-{index:02}.png"
			data, digest = _checked_file(package_root, source_relative, pins)
			prior_relative = f"{prior_import.IMAGE_PREFIX}/cabir-handoff/{form_key}/map/walk/frame-{index:02}.png"
			if prior_outputs.get(prior_relative) != data:
				raise ValueError(f"retained map-walk frame differs from refreshed approved input: {source_relative}")
			verified[source_relative] = digest
		for role, source_name in prior_form["icons"].items():
			source_relative = f"approved-inputs/icons/{source_name}"
			data, digest = _checked_file(package_root, source_relative, pins)
			exported, _ = _checked_file(package_root, f"export/icons/{source_name}", pins)
			prior_relative = f"{prior_import.IMAGE_PREFIX}/cabir-handoff/{form_key}/icons/{prior_form['runtimeIcons'][role]}"
			if prior_outputs.get(prior_relative) != data or exported != data:
				raise ValueError(f"retained icon is not byte-identical to approved refreshed input: {source_name}")
			verified[source_relative] = digest
	for index in range(9):
		source_name = f"cabir-fire-angle-{index:02}.png"
		source_relative = f"approved-inputs/projectile/{source_name}"
		data, digest = _checked_file(package_root, source_relative, pins)
		exported, _ = _checked_file(package_root, f"export/projectile/{source_name}", pins)
		prior_relative = f"{prior_import.IMAGE_PREFIX}/cabir-handoff/projectile/frame-{index:02}.png"
		if prior_outputs.get(prior_relative) != data or exported != data:
			raise ValueError(f"retained projectile is not byte-identical to approved refreshed input: {source_name}")
		verified[source_relative] = digest
	return verified


def _add_output(outputs: dict[str, bytes], relative: str, data: bytes) -> None:
	if relative in outputs:
		raise ValueError(f"duplicate complete Cabir output path: {relative}")
	outputs[relative] = data


def _graphics_patch() -> dict:
	creatures = {}
	for form in FORM_SPECS.values():
		creatures[form["creatureId"]] = {
			"set": {
				"animation": form["battleDescriptor"],
				"map": form["mapDescriptor"],
				"mapAttackFromLeft": form["mapAttack"],
				"mapAttackFromRight": form["mapAttack"],
				"iconLarge": form["iconLarge"],
				"iconSmall": form["iconSmall"],
				"missile": {
					"projectile": form["projectile"],
					"attackClimaxFrame": 4,
					"offset": dict(PROJECTILE_OFFSET),
				},
			},
			"remove": [],
		}
	return {"creatures": creatures}


def _descriptor_for_form(form: dict, state_assets: dict[str, bytes], outputs: dict[str, bytes]) -> tuple[bytes, list[int]]:
	sequences = []
	groups = []
	for state in sorted(form["states"], key=lambda item: item.group):
		groups.append(state.group)
		frames = []
		for index in range(state.count):
			source_relative = f"export/animations/{form['sourceForm']}/{state.source_folder}/frame-{index:02}.png"
			image_relative = f"{IMAGE_PREFIX}/{form['battleBase']}{state.source_folder}/frame-{index:02}.png"
			_add_output(outputs, image_relative, state_assets[source_relative])
			frames.append(f"{state.source_folder}/frame-{index:02}.png")
		sequences.append({"group": state.group, "generateOverlay": 1, "frames": frames})
	descriptor = {"basepath": form["battleBase"], "sequences": sequences}
	return _json_bytes(descriptor), groups


def assemble_overlay(
	state_assets: dict[str, bytes],
	state_hashes: dict[str, str],
	prior_outputs: dict[str, bytes],
	prior_metadata: dict,
	refreshed_source_hashes: dict[str, str],
) -> tuple[dict[str, bytes], dict]:
	"""Assemble already-verified handoff bytes into a private New Horizons overlay."""
	outputs = _select_preserved_prior_outputs(prior_outputs)
	form_reports = {}
	for form_key, form in FORM_SPECS.items():
		descriptor, groups = _descriptor_for_form(form, state_assets, outputs)
		descriptor_path = f"{SPRITE_PREFIX}/{form['battleDescriptor'].replace('.def', '.json')}"
		_add_output(outputs, descriptor_path, descriptor)
		form_reports[form_key] = {
			"creatureId": form["creatureId"],
			"battleDescriptor": form["battleDescriptor"],
			"actualAuthoredGroupIds": groups,
			"actualGroupCount": len(groups),
			"nativeFrameCount": sum(state.count for state in form["states"]),
			"aliasesEmitted": False,
		}

	patch = _json_bytes(_graphics_patch())
	_add_output(outputs, PATCH_NAME, patch)
	all_groups = sorted({state.group for form in FORM_SPECS.values() for state in form["states"]})
	metadata = {
		"schemaVersion": 1,
		"status": "PRIVATE COMPLETE-GROUP CABIR RESOURCE CANDIDATE; NOT INSTALLED OR PLAYABLE",
		"sourcePackage": "output/handoff-20261007-refreshed/cabir",
		"sourcePackageManifestSha256": EXPECTED_PACKAGE_MANIFEST_SHA256,
		"sourceExportManifestSha256": EXPECTED_EXPORT_MANIFEST_SHA256,
		"retainedPriorPackage": "output/handoff-20261007/cabir",
		"retainedPriorHandoffManifestSha256": prior_metadata.get("handoffManifestSha256"),
		"distributionNote": "Derived game-asset pixels remain private review output; no approval to redistribute is implied.",
		"aestheticApproval": "pending; package explicitly remains a review candidate",
		"runtimeInstalled": False,
		"gameplayChanged": False,
		"graphicsPatch": PATCH_NAME,
		"creatures": form_reports,
		"allAuthoredGroupIds": all_groups,
		"groupPixelPolicy": "Direct byte copies of the package's 450x400 static RGBA PNG frames; generateOverlay=1 requests selection overlays.",
		"optionalGroupsNotEmitted": {
			str(group): {"name": name, "engineBehavior": OPTIONAL_GROUP_NOTES[group]}
			for group, name in ENGINE_GROUPS
			if group in OPTIONAL_GROUP_NOTES
		},
		"baseSpecialFrontGroup": "not supplied for base Cabir; its current creature definition has no repair special; no false group-18 alias emitted",
		"turnContract": "Engine group 7, flip facing, then group 8; full-turn review sequences have no engine group and are not installed.",
		"rangedRelease": {
			"attackClimaxFrameOneBased": 4,
			"mouthCanvasXY": MOUTH_CANVAS,
			"projectileAngleFrameZeroBased": PROJECTILE_ANGLE_FRAME,
			"projectileFrameAngles": FRAME_ANGLES,
			"offset": PROJECTILE_OFFSET,
			"note": "Offsets are the explicitly calibrated up/front/down mouth registrations; projectile files and palette are retained byte-exact from the prior package.",
		},
		"retainedPriorResources": {
			"map": "prior approved map-walk images/descriptors; every walk frame was cross-checked byte-exact against refreshed approved-inputs",
			"icons": "prior approved large/small icons; cross-checked byte-exact against refreshed approved-inputs and export icons",
			"projectile": "prior approved nine-angle projectile and descriptor; cross-checked byte-exact against refreshed approved-inputs and export projectiles",
			"sourceHashes": dict(sorted(refreshed_source_hashes.items())),
		},
		"sourceHashes": dict(sorted(state_hashes.items())),
		"outputHashes": {relative: _sha256(data) for relative, data in sorted(outputs.items())},
	}
	outputs[MANIFEST_NAME] = _json_bytes(metadata)
	return outputs, metadata


def build_overlay(
	package_dir: Path = DEFAULT_PACKAGE,
	prior_package_dir: Path = DEFAULT_PRIOR_PACKAGE,
	*,
	expected_package_sha256: str | None = EXPECTED_PACKAGE_MANIFEST_SHA256,
	expected_export_sha256: str | None = EXPECTED_EXPORT_MANIFEST_SHA256,
	expected_package_file_count: int | None = EXPECTED_PACKAGE_FILE_COUNT,
	prior_outputs_override: dict[str, bytes] | None = None,
	prior_metadata_override: dict | None = None,
) -> tuple[dict[str, bytes], dict]:
	state_assets, state_hashes, source = _load_refreshed_package(
		package_dir,
		expected_package_sha256=expected_package_sha256,
		expected_export_sha256=expected_export_sha256,
		expected_file_count=expected_package_file_count,
	)
	if prior_outputs_override is None:
		prior_outputs, prior_metadata = prior_import.build_overlay(
			prior_package_dir,
			expected_manifest_sha256=EXPECTED_PRIOR_HANDOFF_MANIFEST_SHA256,
		)
	else:
		prior_outputs = prior_outputs_override
		prior_metadata = prior_metadata_override or {}
	if prior_metadata.get("handoffManifestSha256") != EXPECTED_PRIOR_HANDOFF_MANIFEST_SHA256:
		raise ValueError("retained prior Cabir package identity changed")
	refreshed_source_hashes = _assert_prior_resources_match_refreshed_inputs(
		prior_outputs, source["packageRoot"], source["packagePins"]
	)
	return assemble_overlay(state_assets, state_hashes, prior_outputs, prior_metadata, refreshed_source_hashes)


def _safe_output(output_dir: Path) -> Path:
	requested = Path(os.path.abspath(output_dir))
	build_root = (ROOT / "build").resolve()
	if requested.is_symlink():
		raise ValueError("output directory must not be a symlink")
	resolved = requested.resolve()
	try:
		relative = resolved.relative_to(build_root)
	except ValueError as error:
		raise ValueError("private Cabir output must stay below repository build/") from error
	if not relative.parts:
		raise ValueError("refusing to use the whole build/ directory as output")
	current = build_root
	for part in relative.parts[:-1]:
		current = current / part
		if current.exists() and current.is_symlink():
			raise ValueError("output path contains a symlink directory")
	if (ROOT / "Mods").resolve() in resolved.parents:
		raise ValueError("private output must not write into the live Mods tree")
	return resolved


def _check_or_write_outputs(outputs: dict[str, bytes], destination: Path, *, check: bool) -> None:
	if check:
		if destination.is_symlink() or not destination.is_dir():
			raise ValueError("--check requires an existing non-symlink output directory")
		actual_paths = set()
		for path in destination.rglob("*"):
			if path.is_symlink():
				raise ValueError(f"output contains a symlink: {path.relative_to(destination)}")
			if path.is_file():
				actual_paths.add(path.relative_to(destination).as_posix())
		if actual_paths != set(outputs):
			missing = sorted(set(outputs) - actual_paths)
			extra = sorted(actual_paths - set(outputs))
			raise ValueError(f"output file inventory differs; missing={missing}, extra={extra}")
		for relative, expected in outputs.items():
			if (destination / relative).read_bytes() != expected:
				raise ValueError(f"output differs from pinned source: {relative}")
		return
	if destination.exists():
		raise ValueError(f"refusing to overwrite existing output directory: {destination}; use --check")
	destination.parent.mkdir(parents=True, exist_ok=True)
	destination.mkdir()
	for relative, data in sorted(outputs.items()):
		rel = _safe_relative(relative)
		path = destination.joinpath(*rel.parts)
		path.parent.mkdir(parents=True, exist_ok=True)
		path.write_bytes(data)


def write_overlay(
	package_dir: Path,
	prior_package_dir: Path,
	output_dir: Path,
	*,
	check: bool = False,
) -> dict:
	outputs, metadata = build_overlay(package_dir, prior_package_dir)
	destination = _safe_output(output_dir)
	_check_or_write_outputs(outputs, destination, check=check)
	return metadata


def main(argv: list[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--package", type=Path, default=DEFAULT_PACKAGE)
	parser.add_argument("--prior-package", type=Path, default=DEFAULT_PRIOR_PACKAGE)
	parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
	parser.add_argument("--check", action="store_true", help="verify an existing private output without changing it")
	args = parser.parse_args(argv)
	try:
		metadata = write_overlay(args.package, args.prior_package, args.output, check=args.check)
	except (OSError, ValueError) as error:
		print(f"Cabir complete handoff import error: {error}", file=sys.stderr)
		return 2
	print(
		f"{metadata['status']}: {sum(item['nativeFrameCount'] for item in metadata['creatures'].values())} authored frames; "
		f"{len(metadata['outputHashes'])} output files at {args.output}"
	)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
