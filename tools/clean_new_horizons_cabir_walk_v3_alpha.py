#!/usr/bin/env python3
"""Create a pinned, alpha-only cleanup and native preview for barehanded Cabir walk v3.

The authored 2x2 source atlas is immutable. This exporter reuses the reviewed
largest-component cleanup, with a source-specific three-pixel safety dilation
to retain every non-faint edge pixel in this candidate, then invokes the
existing fixed-scale animation exporter. It does not install runtime assets.
"""

import argparse
from io import BytesIO
import hashlib
import json
import os
from pathlib import Path
import stat

from PIL import Image

import clean_new_horizons_cabir_alpha as alpha_cleaner
import export_new_horizons_cabir_animation as animation_exporter


ROOT = Path(__file__).resolve().parents[1]
CHECKOUT = ROOT


def _require_external_path(path: Path) -> Path:
    requested = path.expanduser()
    resolved = requested.resolve()
    if (requested.absolute().is_relative_to(CHECKOUT)
            or resolved.is_relative_to(CHECKOUT) or CHECKOUT.is_relative_to(resolved)):
        raise ValueError("authoring requires an explicit external workspace outside the checkout")
    if any(parent.is_symlink() for parent in (requested, *requested.parents)):
        raise ValueError("authoring paths must not traverse symlinks")
    return resolved


def configure_private_root(private_root: Path) -> None:
    """Rebase project-shaped authoring paths without changing the checkout."""
    global ROOT
    resolved = _require_external_path(private_root)
    previous = ROOT
    for name, value in list(globals().items()):
        if name not in ("ROOT", "CHECKOUT") and isinstance(value, Path) and value.is_absolute():
            if value.is_relative_to(previous):
                globals()[name] = resolved / value.relative_to(previous)
    ROOT = resolved

WALK_DIRECTORY = ROOT / "assets/new-horizons/creatures/cabir/v3/walk-v2"
SOURCE_ATLAS = WALK_DIRECTORY / "candidate-01.png"
SOURCE_PROMPT = WALK_DIRECTORY / "candidate-01.prompt.txt"
OUTPUT_DIRECTORY = WALK_DIRECTORY / "cleaned"
OUTPUT_ATLAS_NAME = "candidate-01-alpha-cleaned.png"
RECEIPT_NAME = "cleanup-receipt.json"
PINNED_SOURCE_SHA256 = "17224d2db53658eea8c97e355e43fdb324c60a82b3d845c00a5f6eac33ab7e26"
PINNED_PROMPT_SHA256 = "255e5baf86275a2d7a91369841b054e896b2d9f006d940c2bc7b45a55d842272"
MAX_SOURCE_BYTES = 16 * 1024 * 1024
COLUMNS = 2
ROWS = 2
BODY_HEIGHT = 60
DILATION_RADIUS = 3


def _path_label(path: Path) -> str:
    try:
        return str(path.resolve().relative_to(ROOT))
    except ValueError:
        return path.name


def _read_pinned_source(source: Path) -> tuple[Image.Image, str]:
    """Check exact source identity and PNG header limits before pixel decoding."""
    requested = source.expanduser()
    if requested.is_symlink():
        raise ValueError("source symlinks are not accepted")
    if requested.resolve() != SOURCE_ATLAS.resolve():
        raise ValueError(f"source must be the pinned atlas {_path_label(SOURCE_ATLAS)}")

    try:
        with requested.open("rb") as stream:
            if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
                raise ValueError("source atlas must be a regular file")
            source_bytes = stream.read(MAX_SOURCE_BYTES + 1)
    except OSError as error:
        raise ValueError(f"could not read pinned source atlas: {requested.name}") from error

    if len(source_bytes) > MAX_SOURCE_BYTES:
        raise ValueError("source atlas exceeds the safe byte limit")
    source_sha256 = hashlib.sha256(source_bytes).hexdigest()
    if source_sha256 != PINNED_SOURCE_SHA256:
        raise ValueError("pinned Cabir walk atlas bytes changed; manual review is required")

    try:
        opened = Image.open(BytesIO(source_bytes))
    except (OSError, ValueError) as error:
        raise ValueError("pinned source is not a readable image") from error

    with opened:
        animation_exporter._validate_dimensions(opened.width, opened.height, COLUMNS, ROWS)
        if opened.mode != "RGBA" or opened.format != "PNG" or getattr(opened, "is_animated", False):
            raise ValueError("source must be one static PNG atlas with a real RGBA channel")
        return opened.copy(), source_sha256


def _pinned_prompt_sha256() -> str:
    prompt = SOURCE_PROMPT
    if prompt.is_symlink() or not prompt.is_file():
        raise ValueError("pinned source prompt is missing or is a symlink")
    prompt_sha256 = hashlib.sha256(prompt.read_bytes()).hexdigest()
    if prompt_sha256 != PINNED_PROMPT_SHA256:
        raise ValueError("pinned source prompt changed")
    return prompt_sha256


def export_review_bundle(input_path: Path | None = None, output_dir: Path | None = None) -> Path:
    """Write a new alpha-cleaned copy, 4 native frames and review previews."""
    input_path = SOURCE_ATLAS if input_path is None else input_path
    output_dir = OUTPUT_DIRECTORY if output_dir is None else output_dir
    _require_external_path(output_dir)
    image, source_sha256 = _read_pinned_source(input_path)
    prompt_sha256 = _pinned_prompt_sha256()

    requested_output = output_dir.expanduser()
    output = animation_exporter.validate_new_output_directory(input_path, requested_output)

    cleaned, receipt = alpha_cleaner.clean_atlas(
        image,
        source_sha256=source_sha256,
        dilation_radius=DILATION_RADIUS,
    )
    receipt["sourcePath"] = _path_label(SOURCE_ATLAS)
    receipt["status"] = "provisional generated-sheet alpha-only cleanup; visual review required"
    receipt["sourcePromptPath"] = _path_label(SOURCE_PROMPT)
    receipt["sourcePromptSha256"] = prompt_sha256
    receipt["cleanupRevision"] = "cabir-walk-v3-specific-radius-3"

    output.mkdir(parents=True, exist_ok=False)
    cleaned_path = output / OUTPUT_ATLAS_NAME
    cleaned.save(cleaned_path, format="PNG")
    receipt["cleanedRgbaSha256"] = hashlib.sha256(cleaned.tobytes()).hexdigest()
    receipt["cleanedPngSha256"] = hashlib.sha256(cleaned_path.read_bytes()).hexdigest()
    receipt["cleanedPath"] = _path_label(cleaned_path)

    native_output = output / "native-export"
    animation_exporter.export_animation(
        cleaned_path,
        native_output,
        columns=COLUMNS,
        height=BODY_HEIGHT,
        rows=ROWS,
    )
    receipt["nativeExportPath"] = _path_label(native_output)
    receipt["nativeFiles"] = {
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(native_output.iterdir())
        if path.is_file()
    }

    if hashlib.sha256(SOURCE_ATLAS.read_bytes()).hexdigest() != source_sha256:
        raise RuntimeError("source atlas changed during cleanup/export")
    (output / RECEIPT_NAME).write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", required=True, type=Path,
                        help="external project-shaped authoring workspace")
    parser.add_argument("--input", type=Path, help="pinned immutable RGBA source atlas")
    parser.add_argument("--output-dir", type=Path, help="new external provisional output directory")
    args = parser.parse_args()
    try:
        configure_private_root(args.project_root)
        output = export_review_bundle(args.input, args.output_dir)
    except (FileExistsError, OSError, ValueError, RuntimeError) as error:
        parser.error(str(error))

    receipt = json.loads((output / RECEIPT_NAME).read_text(encoding="utf-8"))
    print(
        f"Created provisional Cabir walk-v3 cleanup: {output} "
        f"({receipt['removedPixelCount']} alpha pixels cleared, "
        f"maximum removed alpha {receipt['maximumRemovedAlpha']})"
    )


if __name__ == "__main__":
    main()
