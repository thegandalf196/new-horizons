#!/usr/bin/env python3
"""Mechanically crop/reduce the Academy v2 masters to native town-icon canvases."""

from hashlib import sha256
from io import BytesIO
from pathlib import Path
import argparse

from PIL import Image


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

V2 = ROOT / "assets/new-horizons/academy/icon-revisions/v2"
CHECK = False


def png_bytes(
    master_path: Path,
    box: tuple[int, int, int, int],
    size: tuple[int, int],
    *,
    black_border: bool = False,
) -> bytes:
    with Image.open(master_path) as master:
        if master.size != (1194, 1317):
            raise ValueError(f"Unexpected master canvas {master_path}: {master.size}")
        crop = master.crop(box)
        content_size = (size[0] - 2, size[1] - 2) if black_border else size
        if crop.width * content_size[1] != crop.height * content_size[0]:
            raise ValueError(f"Crop {box} does not match target aspect {size}")
        output = crop.resize(content_size, Image.Resampling.LANCZOS).convert("RGBA")
        if black_border:
            framed = Image.new("RGBA", size, (0, 0, 0, 255))
            framed.alpha_composite(output, (1, 1))
            output = framed
        buffer = BytesIO()
        output.save(buffer, format="PNG", optimize=False)
        return buffer.getvalue()


def save_or_check(relative: str, payload: bytes) -> None:
    path = _require_external_path(V2 / relative)
    if not path.is_relative_to(V2.resolve()):
        raise ValueError("icon output escapes the explicit workspace")
    if CHECK:
        if not path.is_file() or path.read_bytes() != payload:
            raise SystemExit(f"Stale or missing export: {path}")
    else:
        if path.exists():
            raise FileExistsError("refusing to overwrite an existing icon export")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
    print(f"{path.relative_to(ROOT)} sha256={sha256(payload).hexdigest()}")


# Large: centered 1189x1312 crop (the largest integer crop at exactly 58:64).
# Small icons match native ITPA's opaque 1px black perimeter. Content crops are
# exact 46:30 and stay centered; fort keeps spires and village keeps its dome.
def main() -> None:
    global CHECK
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", required=True, type=Path,
                        help="external project-shaped authoring workspace")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        configure_private_root(args.project_root)
        CHECK = args.check
        small_top = {"fort": 0, "village": 180}
        artifacts = {}
        for town in ("fort", "village"):
            master = V2 / "masters" / f"{town}.png"
            artifacts["exports/NH_academy_" + town + "_large_normal.png"] = png_bytes(
                master, (2, 2, 1191, 1314), (58, 64))
            top = small_top[town]
            artifacts["exports/NH_academy_" + town + "_small_normal.png"] = png_bytes(
                master, (22, top, 1172, top + 750), (48, 32), black_border=True)
        for relative in artifacts:
            path = _require_external_path(V2 / relative)
            if not CHECK and path.exists():
                raise FileExistsError("refusing to overwrite an existing icon export")
        for relative, payload in artifacts.items():
            save_or_check(relative, payload)
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
