"""Synthetic NHART tests; no selected artwork or workstation paths required."""

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location("nhart", Path(__file__).resolve().parents[1] / "nhart.py")
nhart = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(nhart)


class NHArtTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.inputs = self.root / "inputs"
        self.inputs.mkdir()
        self.output = self.root / "selected.nhart"
        self.payloads = {"sprites/unit.png": b"synthetic\x00payload\xff", "sprites/unit.json": b'{"groups":[]}'}
        self.manifest = {"format": 1, "requiredFamilies": ["creature"], "requiredFamilyCounts": {"creature": 2}, "entries": []}
        for name, payload in self.payloads.items():
            source = self.inputs / name
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_bytes(payload)
            self.manifest["entries"].append({"resource": name, "source": name, "size": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(), "family": "creature", "origin": "synthetic fixture",
                "selection": "explicit test selection", "approval": "test-only, not runtime art"})

    def build(self):
        return nhart.pack(self.manifest, self.inputs, self.output)

    def reject_bytes(self, data):
        self.output.write_bytes(data)
        with self.assertRaises(nhart.NHArtError):
            nhart.verify(self.output)

    def test_roundtrip_payloads_and_selfcontained_inventory(self):
        result = self.build()
        data = self.output.read_bytes()
        for row in result["entries"]:
            if row["resource"] in self.payloads:
                self.assertEqual(data[row["offset"]:row["offset"] + row["size"]], self.payloads[row["resource"]])
        for source in self.inputs.rglob("*"):
            if source.is_file():
                source.unlink()
        self.assertEqual(nhart.verify(self.output)["manifest"], nhart.validate_manifest(self.manifest))

    def test_deterministic_order_no_mtime_dependency(self):
        self.build()
        first = self.output.read_bytes()
        self.manifest["entries"].reverse()
        import os
        for name in self.payloads:
            os.utime(self.inputs / name, (1, 1))
        self.build()
        self.assertEqual(self.output.read_bytes(), first)

    def test_bad_headers_and_truncation(self):
        self.build()
        original = self.output.read_bytes()
        for position, value, fmt in ((0, b"BADMAGIC", "8s"), (8, 2, "I"), (12, 1, "I"),
                                     (16, 31, "Q"), (16, len(original) + 1, "Q"),
                                     (24, 0, "Q"), (24, nhart.MAX_ENTRIES + 1, "Q")):
            with self.subTest(position=position, value=value):
                data = bytearray(original)
                struct.pack_into("<" + fmt, data, position, value)
                self.reject_bytes(data)
        for data in (b"", original[:31], original[:-1], original + b"trailing"):
            self.reject_bytes(data)

    def test_index_reserved_names_bounds_overlap_and_digest(self):
        result = self.build()
        original = self.output.read_bytes()
        index = result["indexOffset"]
        # First record is metadata. Validate every fixed field before payload use.
        for relative, value, fmt in ((0, 0, "I"), (0, 4097, "I"), (4, 1, "I"),
                                      (8, 0, "Q"), (8, index + 1, "Q"), (16, 2**64 - 1, "Q")):
            data = bytearray(original)
            struct.pack_into("<" + fmt, data, index + relative, value)
            self.reject_bytes(data)
        second = index + nhart.RECORD.size + len(nhart.MANIFEST)
        data = bytearray(original)
        struct.pack_into("<Q", data, second + 8, nhart.HEADER.size)
        self.reject_bytes(data)
        for replacement in (b"/nhart/manifest.json", b"\\nhart/manifest.json", b"\xffnhart/manifest.json"):
            data = bytearray(original)
            self.assertEqual(len(replacement), len(nhart.MANIFEST))
            data[index + nhart.RECORD.size:index + nhart.RECORD.size + len(replacement)] = replacement
            self.reject_bytes(data)
        data = bytearray(original)
        data[nhart.HEADER.size] ^= 1
        self.reject_bytes(data)

    def test_manifest_hash_is_independent_of_index_hash(self):
        result = self.build()
        data = bytearray(self.output.read_bytes())
        row = next(row for row in result["entries"] if row["resource"].endswith(".png"))
        data[row["offset"]] ^= 1
        cursor = result["indexOffset"]
        for current in result["entries"]:
            if current == row:
                digest = hashlib.sha256(data[row["offset"]:row["offset"] + row["size"]]).digest()
                data[cursor + 24:cursor + 56] = digest
            cursor += nhart.RECORD.size + len(current["resource"].encode())
        self.reject_bytes(data)

    def test_invalid_manifest_and_resource_collisions(self):
        for replacement in ([], None):
            manifest = copy.deepcopy(self.manifest)
            manifest["entries"] = replacement
            with self.assertRaises(nhart.NHArtError):
                nhart.validate_manifest(manifest)
        for name in ("/sprite.png", "../sprite.png", "a/./sprite.png", "a//sprite.png",
                     "C:/sprite.png", "a\\sprite.png", "a\x00.png", nhart.MANIFEST, ".NHART/MANIFEST.JSON"):
            manifest = copy.deepcopy(self.manifest)
            manifest["entries"][0]["resource"] = name
            with self.subTest(name=name), self.assertRaises(nhart.NHArtError):
                nhart.validate_manifest(manifest)
        manifest = copy.deepcopy(self.manifest)
        extra = dict(manifest["entries"][0], resource="SPRITES/UNIT.BMP")
        manifest["entries"].append(extra)
        with self.assertRaises(nhart.NHArtError):
            nhart.validate_manifest(manifest)
        manifest = copy.deepcopy(self.manifest)
        manifest["entries"].pop()
        with self.assertRaises(nhart.NHArtError):
            nhart.validate_manifest(manifest)
        self.assertNotEqual(nhart.resource_identity("sprites/unit.png"), nhart.resource_identity("sprites/unit.json"))
        self.assertEqual(nhart.resource_identity("a/test.unknown"), ("A/TEST.UNKNOWN", "OTHER"))
        self.assertEqual(nhart.validate_name(".selected/art.png"), b".selected/art.png")

    def test_required_families_and_explicit_provenance(self):
        for field, replacement in (("size", True), ("sha256", "0"), ("approval", ""),
                                   ("origin", "\ud800"), ("source", "../outside")):
            manifest = copy.deepcopy(self.manifest)
            manifest["entries"][0][field] = replacement
            with self.assertRaises(nhart.NHArtError):
                nhart.validate_manifest(manifest)
        manifest = copy.deepcopy(self.manifest)
        manifest["requiredFamilies"].append("town")
        with self.assertRaises(nhart.NHArtError):
            nhart.validate_manifest(manifest)

    def test_failed_inputs_and_staged_verification_preserve_existing_pack(self):
        self.build()
        original = self.output.read_bytes()
        manifest = copy.deepcopy(self.manifest)
        manifest["entries"][0]["sha256"] = "0" * 64
        with self.assertRaises(nhart.NHArtError):
            nhart.pack(manifest, self.inputs, self.output)
        self.assertEqual(self.output.read_bytes(), original)
        with mock.patch.object(nhart, "verify", side_effect=nhart.NHArtError("staged rejection")):
            with self.assertRaises(nhart.NHArtError):
                self.build()
        self.assertEqual(self.output.read_bytes(), original)
        self.assertEqual(list(self.root.glob(".nhart-stage-*")), [])

    def test_source_escape_and_output_source_rejected(self):
        source = self.inputs / "sprites/unit.png"
        outside = self.root / "outside.png"
        outside.write_bytes(self.payloads["sprites/unit.png"])
        source.unlink()
        source.symlink_to(outside)
        with self.assertRaises(nhart.NHArtError):
            self.build()
        source.unlink()
        source.write_bytes(outside.read_bytes())
        with self.assertRaises(nhart.NHArtError):
            nhart.pack(self.manifest, self.inputs, source)

    def test_empty_payload_allowed_but_not_empty_inventory(self):
        entry = self.manifest["entries"][0]
        (self.inputs / entry["source"]).write_bytes(b"")
        entry.update(size=0, sha256=hashlib.sha256(b"").hexdigest())
        self.build()
        nhart.verify(self.output)

    def test_json_duplicate_members_rejected(self):
        with self.assertRaises(nhart.NHArtError):
            nhart.read_json(b'{"format":1,"format":1}')

    def test_expected_manifest_comparison(self):
        self.build()
        self.manifest["entries"].reverse()
        nhart.verify(self.output, self.manifest)
        expected = self.root / "selected.json"
        expected.write_text(json.dumps(self.manifest), encoding="utf-8")
        nhart.verify(self.output, expected)
        self.manifest["entries"][0]["approval"] = "different review"
        with self.assertRaises(nhart.NHArtError):
            nhart.verify(self.output, self.manifest)

    def test_missing_metadata(self):
        result = self.build()
        original = self.output.read_bytes()
        index = result["indexOffset"]
        data = bytearray(original)
        name = b"xnhart/manifest.json"
        data[index + nhart.RECORD.size:index + nhart.RECORD.size + len(name)] = name
        self.reject_bytes(data)

    def test_forged_index_collision_and_index_cap(self):
        def forged(names):
            payload = b"x"
            index = b"".join(nhart.RECORD.pack(len(name), 0, 32, 0, hashlib.sha256(b"").digest()) + name for name in names)
            return nhart.HEADER.pack(nhart.MAGIC, 1, 0, 33, len(names)) + payload + index
        for names in ((b"a.png", b"A.BMP"), (b".PNG", b".BMP"),
                      (b"a.unknown", b"A.UNKNOWN"), (b"a/../b.png",)):
            self.reject_bytes(forged(names))
        self.build()
        with mock.patch.object(nhart, "MAX_INDEX", 10):
            with self.assertRaises(nhart.NHArtError):
                nhart.verify(self.output)

    def test_native_last_dot_extension_identity(self):
        self.assertEqual(nhart.resource_identity(".PNG"), ("", "IMAGE"))
        self.assertEqual(nhart.resource_identity("folder/.BMP"), ("FOLDER/", "IMAGE"))
        self.assertEqual(nhart.resource_identity("folder.nhart/payload"), ("FOLDER.NHART/PAYLOAD", "OTHER"))
        self.assertEqual(nhart.resource_identity("nested/package.nhart"), ("NESTED/PACKAGE", "ARCHIVE_NHART"))
        manifest = copy.deepcopy(self.manifest)
        manifest["entries"][0]["resource"] = ".PNG"
        manifest["entries"][1]["resource"] = ".BMP"
        with self.assertRaises(nhart.NHArtError):
            nhart.validate_manifest(manifest)

    def test_distribution_rejects_nonvisual_payload_types(self):
        for suffix in ("exe", "zip", "lod", "nhart", "wav", "lua", "unknown"):
            manifest = copy.deepcopy(self.manifest)
            manifest["entries"][0]["resource"] = "selected/payload." + suffix
            with self.subTest(suffix=suffix), self.assertRaises(nhart.NHArtError):
                nhart.validate_manifest(manifest)
        for suffix in ("png", "def", "msk", "pal", "fnt", "ttf", "json", "txt"):
            manifest = copy.deepcopy(self.manifest)
            manifest["entries"][0]["resource"] = "selected/payload." + suffix
            nhart.validate_manifest(manifest)

    def test_symlinked_source_directory_escaping_root_rejected(self):
        outside = self.root / "external-source-directory"
        outside.mkdir()
        (outside / "payload.png").write_bytes(self.payloads["sprites/unit.png"])
        (self.inputs / "linked").symlink_to(outside, target_is_directory=True)
        manifest = copy.deepcopy(self.manifest)
        manifest["entries"][0]["source"] = "linked/payload.png"
        with self.assertRaises(nhart.NHArtError):
            nhart.pack(manifest, self.inputs, self.output)
        (self.inputs / "linked").unlink()
        (self.inputs / "linked").symlink_to(self.inputs / "sprites", target_is_directory=True)
        manifest["entries"][0]["source"] = "linked/unit.png"
        with self.assertRaises(nhart.NHArtError):
            nhart.pack(manifest, self.inputs, self.output)


if __name__ == "__main__":
    unittest.main()
