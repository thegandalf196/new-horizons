#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read-only gzip/header audit of authored command GUI fixtures, not game acceptance.

Hero/spell/army legality must additionally pass the native parser/initialization
export assertions and the independent frozen-candidate GUI journey.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

EXPECTED_NAMES = {"NHCommandsBooklessAI", "NHCommandsSpellAI"}
MAX_BYTES = 8 * 1024 * 1024


def inspect(data):
    if len(data) > MAX_BYTES:
        raise ValueError("Compressed fixture exceeds bounded audit size")
    stream = zlib.decompressobj(16 + zlib.MAX_WBITS)
    raw = stream.decompress(data, MAX_BYTES + 1)
    if len(raw) > MAX_BYTES or not stream.eof or stream.unused_data or stream.unconsumed_tail:
        raise ValueError("Oversized, truncated or concatenated gzip fixture")
    # SOD header: uint32 version, uint8 hasPlayers, uint32 size,
    # uint8 underground, length-prefixed name and description.
    if len(raw) < 14:
        raise ValueError("Short map header")
    version, = struct.unpack_from("<I", raw)
    size, = struct.unpack_from("<I", raw, 5)
    if version != 0x1c or raw[4] != 1 or size != 36 or raw[9] != 0:
        raise ValueError("Expected authored SOD/36x36/one-level playable fixture")
    offset = 10
    strings = []
    for _ in range(2):
        length, = struct.unpack_from("<I", raw, offset)
        offset += 4
        if length > 4096 or offset + length > len(raw):
            raise ValueError("Invalid bounded header string")
        strings.append(raw[offset:offset + length].decode("utf-8"))
        offset += length
    if strings[0] not in EXPECTED_NAMES:
        raise ValueError("Not an approved command hero-versus-hero fixture name")
    return {"name": strings[0], "format": "SOD", "map_size": 36, "levels": 1,
            "compressed_bytes": len(data), "raw_bytes": len(raw),
            "gzip_sha256": hashlib.sha256(data).hexdigest(),
            "raw_sha256": hashlib.sha256(raw).hexdigest()}


def self_test():
    def field(s):
        b = s.encode()
        return struct.pack("<I", len(b)) + b
    raw = struct.pack("<IBIB", 0x1c, 1, 36, 0) + field("NHCommandsBooklessAI") + field("header-only control")
    encoder = zlib.compressobj(wbits=16 + zlib.MAX_WBITS)
    good = encoder.compress(raw) + encoder.flush()
    assert inspect(good)["name"] == "NHCommandsBooklessAI"
    for bad in (good[:-1], good + good, b"not gzip"):
        try:
            inspect(bad)
        except (ValueError, zlib.error):
            continue
        raise AssertionError("Malformed gzip accepted")
    print("PASS: header-only control accepted; truncated/concatenated/invalid gzip rejected")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("maps", type=Path, nargs="*")
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    if args.maps:
        if not args.manifest:
            parser.error("--manifest required with actual maps")
        records = []
        for path in args.maps:
            if path.stat().st_size > MAX_BYTES:
                raise ValueError("Fixture file exceeds audit bound")
            records.append({"file": path.name, **inspect(path.read_bytes())})
        if {record["name"] for record in records} != EXPECTED_NAMES or len(records) != 2:
            raise ValueError("Both distinct authored hero-versus-hero fixtures required")
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        args.manifest.write_text(json.dumps({"maps": records,
            "scope": "Gzip integrity/header/hash only; native semantic assertions and GUI acceptance separately required"}, indent=2) + "\n")
        print("PASS: both actual fixture gzip streams, headers and hashes audited")
    elif not args.self_test:
        parser.error("Supply both maps or --self-test")


if __name__ == "__main__":
    main()
