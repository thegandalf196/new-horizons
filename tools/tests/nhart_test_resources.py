"""Read shipping NHART resources in tests without extracting loose files."""

from functools import lru_cache
from io import BytesIO
import hashlib
import importlib.util
from PIL import Image
from pathlib import Path, PurePosixPath
import json

ROOT = Path(__file__).resolve().parents[2]
ARCHIVE = ROOT / "Mods/new-horizons/NewHorizons.nhart"
MANIFEST = ROOT / "assets/new-horizons/runtime-art-manifest.json"


def later_art_waves():
    """Explicit additive selection ledger, absent on pre-ledger revisions."""
    path = ROOT / 'assets/new-horizons/runtime-art-additions.json'
    return json.loads(path.read_text(encoding='utf-8'))['waves'] if path.exists() else []


def later_art_resources():
    names = [name for wave in later_art_waves() for name in wave['resources']]
    if len(names) != len(set(names)):
        raise ValueError('Duplicate later-art resource selection')
    return set(names)


def entries_before_wave(entries, waves, index):
    """Undo selected additions/replacements without rewriting historical anchors."""
    previous = {row['resource']: row for row in entries}
    if len(previous) != len(entries):
        raise ValueError('Duplicate selected resource')
    for wave in reversed(waves[index:]):
        for replacement in wave.get('replacedEntries', []):
            old, current = replacement['previous'], replacement['current']
            name = current['resource']
            if old['resource'] != name or previous.get(name) != current or name in wave['resources']:
                raise ValueError('Broken resource replacement history')
            previous[name] = old
        for name in wave['resources']:
            if name not in previous:
                raise ValueError('Missing added resource in history')
            del previous[name]
    return list(previous.values())


def resource_before_wave(name, waves, index, reader=None):
    """Reconstruct a descriptor's exact old bytes from a reversible append."""
    payload = (reader or read_resource)(name)
    for wave in reversed(waves[index:]):
        for replacement in reversed(wave.get('replacedEntries', [])):
            current, old = replacement['current'], replacement['previous']
            if current['resource'] != name:
                continue
            if old['resource'] != name or len(payload) != current['size'] or hashlib.sha256(payload).hexdigest() != current['sha256']:
                raise ValueError('Current replacement payload mismatch')
            if not name.endswith('.json') or set(replacement) != {'previous', 'current', 'removeAppendedSequence'}:
                raise ValueError('Unsupported reversible resource change')
            descriptor = json.loads(payload)
            sequences = descriptor.get('sequences', [])
            added = replacement['removeAppendedSequence']
            if not sequences or sequences[-1] != added or any(row.get('group') == added['group'] for row in sequences[:-1]):
                raise ValueError('Appended sequence does not match history')
            sequences.pop()
            payload = (json.dumps(descriptor, indent=2) + '\n').encode()
            if len(payload) != old['size'] or hashlib.sha256(payload).hexdigest() != old['sha256']:
                raise ValueError('Historical replacement payload mismatch')
    return payload

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
