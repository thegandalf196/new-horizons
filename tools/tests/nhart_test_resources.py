"""Read shipping NHART resources in tests without extracting loose files."""

from functools import lru_cache
from io import BytesIO
import hashlib
import importlib.util
from PIL import Image
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[2]
ARCHIVE = ROOT / "Mods/new-horizons/NewHorizons.nhart"
MANIFEST = ROOT / "assets/new-horizons/runtime-art-manifest.json"
_spec = importlib.util.spec_from_file_location("nhart_test_reader", ROOT / "tools/nhart.py")
_reader = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_reader)


@lru_cache(maxsize=1)
def verified_index():
    """Hash and inventory-check the shipping archive once per test process."""
    result = _reader.verify(ARCHIVE, MANIFEST)
    return {row["resource"]: row for row in result["entries"] if row["resource"] != _reader.MANIFEST}


def resource_names(prefix=""):
    return {name for name in verified_index() if name.startswith(prefix)}


@lru_cache(maxsize=None)
def read_resource(name):
    row = verified_index()[name]
    with ARCHIVE.open("rb") as stream:
        stream.seek(row["offset"])
        payload = stream.read(row["size"])
    if len(payload) != row["size"] or hashlib.sha256(payload).hexdigest() != row["sha256"]:
        raise ValueError("Shipping NHART changed after verification")
    return payload


def open_image(name):
    """Decode verified shipping bytes, without a filesystem art fallback."""
    image = Image.open(BytesIO(read_resource(name)))
    image.load()
    return image


class ArtPath:
    """Small read-only path facade for existing payload-oriented assertions."""

    def __init__(self, resource="SPRITES"):
        self.resource = str(resource).rstrip("/")

    def __truediv__(self, component):
        return ArtPath(self.resource + "/" + PurePosixPath(component).as_posix())

    def is_file(self):
        return self.resource in verified_index()

    def read_bytes(self):
        return read_resource(self.resource)

    def read_text(self, encoding="utf-8"):
        return self.read_bytes().decode(encoding)

    @property
    def name(self):
        return PurePosixPath(self.resource).name

    @property
    def stem(self):
        return PurePosixPath(self.resource).stem

    def with_suffix(self, suffix):
        return ArtPath(PurePosixPath(self.resource).with_suffix(suffix))

    def open_image(self):
        return open_image(self.resource)

    def glob(self, pattern):
        prefix = self.resource + "/"
        return [ArtPath(name) for name in sorted(resource_names(prefix))
                if "/" not in name[len(prefix):] and PurePosixPath(name[len(prefix):]).match(pattern)]

    def __str__(self):
        return "NHART:" + self.resource
