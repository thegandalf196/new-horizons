#!/usr/bin/env python3
"""Create a pinned, alpha-only provisional Cabir Master walk export.

The generated atlas and its prompt are immutable inputs. Cleanup and battle
frame rendering reuse the reviewed Cabir cleanup/animation helpers; this file
only binds those helpers to the separately versioned Cabir Master source tree.
"""

import argparse
from io import BytesIO
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import tempfile

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

VERSION_DIRECTORY = ROOT / "assets/new-horizons/creatures/cabir-master/v3"
WALK_DIRECTORY = VERSION_DIRECTORY / "walk-v1"
SOURCE_ATLAS = WALK_DIRECTORY / "candidate-01.png"
SOURCE_PROMPT = WALK_DIRECTORY / "candidate-01.prompt.txt"
REFERENCE_MASTER = VERSION_DIRECTORY / "standing-master.png"
REFERENCE_PROMPT = VERSION_DIRECTORY / "standing.prompt.txt"
OUTPUT_DIRECTORY = WALK_DIRECTORY / "cleaned-v1"

SOURCE_ATLAS_SHA256 = "f38b76a4b929f21dd6c6fd4629c5b8c4f090145fc242cd11fe3abb6d40c7115d"
SOURCE_PROMPT_SHA256 = "088fe9244edce4bcb9401d173aa1b837c7efd31a3fed578e8f9d713721dc6731"
REFERENCE_MASTER_SHA256 = "c1c88872bf0cc8e969f9d883b289aff86071db31241d35a16255b74532fbca15"
REFERENCE_PROMPT_SHA256 = "3b97121db995db93b8ec57e775b147c3edfca9da5d0a3696dd471ff67390a5b7"

MAX_SOURCE_BYTES = 16 * 1024 * 1024
ROWS = 2
COLUMNS = 2
BODY_HEIGHT = 60
DILATION_RADIUS = 3


def _relative(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return path.name


def _read_pinned(path: Path, expected_sha256: str, description: str) -> bytes:
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"{description} must be a regular, non-symlink file")
    try:
        with path.open("rb") as stream:
            if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
                raise ValueError(f"{description} must be a regular file")
            content = stream.read(MAX_SOURCE_BYTES + 1)
    except OSError as error:
        raise ValueError(f"could not read {description}") from error
    if len(content) > MAX_SOURCE_BYTES:
        raise ValueError(f"{description} exceeds the safe byte limit")
    if hashlib.sha256(content).hexdigest() != expected_sha256:
        raise ValueError(f"pinned {description} changed; manual review is required")
    return content


def _read_atlas() -> tuple[Image.Image, bytes]:
    source_bytes = _read_pinned(SOURCE_ATLAS, SOURCE_ATLAS_SHA256, "walk atlas")
    try:
        opened = Image.open(BytesIO(source_bytes))
    except (OSError, ValueError) as error:
        raise ValueError("pinned walk atlas is not a readable image") from error
    with opened:
        animation_exporter._validate_dimensions(opened.width, opened.height, COLUMNS, ROWS)
        if opened.format != "PNG" or opened.mode != "RGBA" or getattr(opened, "is_animated", False):
            raise ValueError("source must be one static PNG atlas with a real RGBA channel")
        return opened.copy(), source_bytes


def build_review_files() -> dict[str, bytes]:
    """Build deterministic files in memory/temp storage without touching inputs."""
    atlas, source_bytes = _read_atlas()
    source_prompt_bytes = _read_pinned(SOURCE_PROMPT, SOURCE_PROMPT_SHA256, "walk prompt")
    reference_master_bytes = _read_pinned(REFERENCE_MASTER, REFERENCE_MASTER_SHA256, "reference master")
    reference_prompt_bytes = _read_pinned(REFERENCE_PROMPT, REFERENCE_PROMPT_SHA256, "reference prompt")

    cleaned, receipt = alpha_cleaner.clean_atlas(
        atlas,
        source_sha256=SOURCE_ATLAS_SHA256,
        dilation_radius=DILATION_RADIUS,
    )
    receipt["status"] = "provisional generated Cabir Master walk export; motion and alpha review pending"
    receipt["sourcePath"] = _relative(SOURCE_ATLAS)
    receipt["sourcePromptPath"] = _relative(SOURCE_PROMPT)
    receipt["sourcePromptSha256"] = hashlib.sha256(source_prompt_bytes).hexdigest()
    receipt["referenceMasterPath"] = _relative(REFERENCE_MASTER)
    receipt["referenceMasterSha256"] = hashlib.sha256(reference_master_bytes).hexdigest()
    receipt["referencePromptPath"] = _relative(REFERENCE_PROMPT)
    receipt["referencePromptSha256"] = hashlib.sha256(reference_prompt_bytes).hexdigest()
    receipt["cleanupRevision"] = "cabir-master-walk-v1-alpha-cleanup-radius-3"
    receipt["sourceFileModified"] = False

    files: dict[str, bytes] = {}
    cleaned_stream = BytesIO()
    cleaned.save(cleaned_stream, format="PNG", optimize=False)
    cleaned_bytes = cleaned_stream.getvalue()
    files["candidate-01-alpha-cleaned.png"] = cleaned_bytes
    receipt["cleanedRgbaSha256"] = hashlib.sha256(cleaned.tobytes()).hexdigest()
    receipt["cleanedPngSha256"] = hashlib.sha256(cleaned_bytes).hexdigest()
    receipt["cleanedPath"] = "candidate-01-alpha-cleaned.png"

    temporary_parent = _require_external_path(SOURCE_ATLAS.parent)
    with tempfile.TemporaryDirectory(prefix="nh-cabir-master-walk-v1-", dir=temporary_parent) as temporary:
        temporary_root = Path(temporary)
        temporary_atlas = temporary_root / "candidate-01-alpha-cleaned.png"
        temporary_atlas.write_bytes(cleaned_bytes)
        temporary_export = temporary_root / "native-export"
        animation_exporter.export_animation(
            temporary_atlas,
            temporary_export,
            columns=COLUMNS,
            rows=ROWS,
            height=BODY_HEIGHT,
        )
        native_files = {
            path.name: path.read_bytes()
            for path in sorted(temporary_export.iterdir())
            if path.is_file()
        }

    receipt["nativeExportPath"] = "native-export"
    receipt["nativeFiles"] = {
        filename: hashlib.sha256(content).hexdigest()
        for filename, content in native_files.items()
    }
    files.update({f"native-export/{filename}": content for filename, content in native_files.items()})
    files["cleanup-receipt.json"] = (json.dumps(receipt, indent=2) + "\n").encode("utf-8")

    if hashlib.sha256(SOURCE_ATLAS.read_bytes()).digest() != hashlib.sha256(source_bytes).digest():
        raise RuntimeError("source atlas changed during cleanup/export")
    return files


def _validate_output_path(output: Path) -> Path:
    requested = output.expanduser()
    _require_external_path(requested)
    if requested.is_symlink() or requested.exists():
        raise FileExistsError(f"output directory must be new: {_relative(requested)}")
    return requested


def export_review_bundle(output: Path | None = None) -> Path:
    """Write a new alpha-cleaned atlas and provisional native review bundle."""
    output = _validate_output_path(OUTPUT_DIRECTORY if output is None else output)
    files = build_review_files()
    staging = Path(tempfile.mkdtemp(prefix=".cleaned-v1-staging-", dir=output.parent))
    try:
        for relative_name, content in files.items():
            destination = staging / relative_name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(content)
        os.rename(staging, output)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    return output


def check_review_bundle(output: Path | None = None) -> None:
    """Verify that the checked-in review bundle exactly matches current pins."""
    requested = (OUTPUT_DIRECTORY if output is None else output).expanduser()
    _require_external_path(requested)
    if requested.is_symlink() or not requested.is_dir():
        raise ValueError("expected an existing external review bundle")
    expected = build_review_files()
    actual_paths = {path.relative_to(requested).as_posix() for path in requested.rglob("*") if path.is_file()}
    if actual_paths != set(expected):
        raise ValueError("review bundle file set differs from deterministic output")
    for relative_name, content in expected.items():
        if (requested / relative_name).read_bytes() != content:
            raise ValueError(f"review bundle differs from pinned deterministic output: {relative_name}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", required=True, type=Path,
                        help="external project-shaped authoring workspace")
    parser.add_argument("--output-dir", type=Path, help="external review bundle directory")
    parser.add_argument("--check", action="store_true", help="verify the existing pinned bundle without writing")
    args = parser.parse_args()
    try:
        configure_private_root(args.project_root)
        if args.check:
            check_review_bundle(args.output_dir)
            print(f"Verified provisional Cabir Master walk export: {_relative(OUTPUT_DIRECTORY)}")
        else:
            output = export_review_bundle(args.output_dir)
            receipt = json.loads((output / "cleanup-receipt.json").read_text(encoding="utf-8"))
            print(
                f"Created provisional Cabir Master walk export: {_relative(output)} "
                f"({receipt['removedPixelCount']} alpha pixels cleared; "
                f"maximum removed alpha {receipt['maximumRemovedAlpha']})"
            )
    except (FileExistsError, OSError, ValueError, RuntimeError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
