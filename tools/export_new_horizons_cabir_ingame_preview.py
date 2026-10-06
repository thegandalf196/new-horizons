#!/usr/bin/env python3
"""Build a detached, temporary in-game Cabir standing-frame preview bundle.

This only emits an overlay for a disposable New Horizons module copy. It never
installs assets into the live module or changes creature gameplay data.
"""

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/new-horizons/creatures/cabir/v1/previews/standing-preview.png"
APPROVED_SOURCE_SHA256 = "11639ea1edfa53012299db76f97ae4bd1a0b432ac4062f84f89ef762a7ad4e83"
LIVE_MODULE = ROOT / "Mods/new-horizons"
PROTECTED_ASSETS = ROOT / "assets/new-horizons"
CREATURES_PATH = Path("Content/config/creatures/cabirPreview.json")
ANIMATION_NAME = "NH_cabir_ingame_preview"
ANIMATION_DIRECTORY = Path("Content/sprites") / ANIMATION_NAME
ANIMATION_DESCRIPTOR = Path("Content/sprites") / f"{ANIMATION_NAME}.json"
NATIVE_IMAGE = ANIMATION_DIRECTORY / "00.png"
LOGICAL_CANVAS = (450, 400)
SOURCE_PLACEMENT = (163, 210)
EXPECTED_ALPHA_BOUNDS = (6, 1, 61, 58)
ALL_CREATURE_GROUPS = (
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


def _within(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def validate_detached_path(output_dir: Path) -> Path:
    resolved = output_dir.resolve()
    protected = (LIVE_MODULE.resolve(), PROTECTED_ASSETS.resolve())
    if any(_within(resolved, protected_root) for protected_root in protected):
        raise ValueError(f"refusing to write or verify inside protected source/module path: {resolved}")
    return resolved


def create_native_frame() -> Image.Image:
    source_bytes = SOURCE.read_bytes()
    if hashlib.sha256(source_bytes).hexdigest() != APPROVED_SOURCE_SHA256:
        raise ValueError("approved Cabir standing preview hash changed; review before exporting")

    with Image.open(SOURCE) as opened:
        sprite = opened.convert("RGBA")
    if sprite.size != (64, 60):
        raise ValueError(f"approved Cabir preview dimensions changed: {sprite.size}")
    if sprite.getchannel("A").getbbox() != EXPECTED_ALPHA_BOUNDS:
        raise ValueError("approved Cabir preview alpha bounds changed; review anchor before exporting")

    canvas = Image.new("RGBA", LOGICAL_CANVAS, (0, 0, 0, 0))
    canvas.alpha_composite(sprite, SOURCE_PLACEMENT)
    if canvas.getchannel("A").getbbox() != (169, 211, 224, 268):
        raise ValueError("Cabir preview no longer matches the approved center and feet baseline")
    return canvas


def expected_patch() -> dict:
    return {
        "core:gremlin": {"graphics": {"animation": ANIMATION_NAME}},
        "core:masterGremlin": {"graphics": {"animation": ANIMATION_NAME}},
    }


def expected_animation_descriptor() -> dict:
    return {
        "basepath": f"{ANIMATION_NAME}/",
        "sequences": [
            {"group": group, "frames": ["00.png"]}
            for group, _name in ALL_CREATURE_GROUPS
        ],
    }


def write_bundle(output_dir: Path) -> Path:
    output_dir = validate_detached_path(output_dir)
    if output_dir.exists() or output_dir.is_symlink():
        raise FileExistsError(f"output directory must be new: {output_dir}")

    native_frame = create_native_frame()
    creatures_path = output_dir / CREATURES_PATH
    descriptor_path = output_dir / ANIMATION_DESCRIPTOR
    frame_path = output_dir / NATIVE_IMAGE
    creatures_path.parent.mkdir(parents=True, exist_ok=True)
    descriptor_path.parent.mkdir(parents=True, exist_ok=True)
    frame_path.parent.mkdir(parents=True, exist_ok=True)

    creatures_path.write_text(json.dumps(expected_patch(), indent=2) + "\n", encoding="utf-8")
    descriptor_path.write_text(json.dumps(expected_animation_descriptor(), indent=2) + "\n", encoding="utf-8")
    native_frame.save(frame_path)
    (output_dir / "PREVIEW_README.md").write_text(
        "# Temporary Cabir in-game preview overlay\n\n"
        "This is a detached graphics-only overlay for a disposable copy of the "
        "New Horizons module. Do not copy it into the live module. In that "
        "isolated module copy, add `config/creatures/cabirPreview.json` to the "
        "module's `creatures` list. The original `core:gremlin` and "
        "`core:masterGremlin` identities, stats, abilities, recruitment, sounds, "
        "and gameplay rules are unchanged. Names remain the existing Gremlin names.\n\n"
        "The approved base Cabir standing image is used for both identities. "
        "Every creature animation group points to that same one-frame sprite, "
        "so no vanilla transition frames are mixed into the preview. This is "
        "temporary and non-animated: the death/corpse pose remains standing, "
        "and movement, attacks, shooting, and turns do not animate. The Master "
        "Gremlin's existing projectile and sounds are not replaced. This is not "
        "a complete creature animation set or runtime acceptance.\n\n"
        "The PNG is the unchanged 64x60 approved preview placed at (163,210) on "
        "the 450x400 logical battle canvas. Its visible alpha bounds are "
        "(169,211)-(224,268), centering the subject at x=196.5 with feet at "
        "baseline y=268.\n",
        encoding="utf-8",
    )
    return output_dir


def verify_bundle(output_dir: Path) -> Path:
    output_dir = validate_detached_path(output_dir)
    if not output_dir.is_dir():
        raise FileNotFoundError(f"preview bundle directory does not exist: {output_dir}")

    patch_path = output_dir / CREATURES_PATH
    descriptor_path = output_dir / ANIMATION_DESCRIPTOR
    frame_path = output_dir / NATIVE_IMAGE
    if json.loads(patch_path.read_text(encoding="utf-8")) != expected_patch():
        raise ValueError("detached creature graphics patch differs from the expected preview-only patch")
    if json.loads(descriptor_path.read_text(encoding="utf-8")) != expected_animation_descriptor():
        raise ValueError("animation descriptor has unexpected groups or frames")

    with Image.open(frame_path) as opened:
        frame = opened.convert("RGBA")
    if frame.size != LOGICAL_CANVAS or frame.getchannel("A").getbbox() != (169, 211, 224, 268):
        raise ValueError("native Cabir preview canvas or anchor does not match the expected geometry")
    if frame.tobytes() != create_native_frame().tobytes():
        raise ValueError("native Cabir preview pixels differ from the approved sprite placement")
    return output_dir


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--verify-only",
        action="store_true",
        help="validate an already generated detached bundle without writing",
    )
    args = parser.parse_args()

    try:
        output_dir = verify_bundle(args.output_dir) if args.verify_only else write_bundle(args.output_dir)
    except (FileExistsError, FileNotFoundError, ValueError) as error:
        parser.error(str(error))
    print(f"{'Verified' if args.verify_only else 'Created'} detached Cabir preview bundle: {output_dir}")


if __name__ == "__main__":
    main()
