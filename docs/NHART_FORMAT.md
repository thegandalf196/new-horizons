# NHART v1

NHART is a deterministic, uncompressed binary resource container, not ZIP.
Its SHA256 hashes detect corruption and inventory drift; they do not authenticate
a publisher or establish distribution rights. Selection and approval remain
explicit reviewed inventory decisions, never inferred by the packer.

## Binary layout

All integers are unsigned little-endian. The 32-byte header is followed by raw
payloads and an index extending exactly to EOF. No timestamps are stored.

| Header offset | Size | Field |
| --- | --- | --- |
| 0 | 8 | Magic bytes `NHART\r\n\x1a` |
| 8 | 4 | Version, exactly 1 |
| 12 | 4 | Flags, exactly 0 |
| 16 | 8 | Absolute index offset |
| 24 | 8 | Entry count, at most 1,000,000 |

Each index record consists of a 4-byte UTF-8 name length, 4-byte reserved zero,
8-byte absolute payload offset, 8-byte payload length, 32 raw SHA256 digest bytes,
and the name bytes (no terminator). Names occupy 1 through 4096 bytes. The entire
index is at most 256 MiB. Payload ranges must lie between the header and index
and must not overlap; zero-length payloads are permitted. Extra index bytes,
unsupported versions/flags and truncated data are rejected.
The native generic container reader permits a zero-entry index. The distribution
packer/verifier described here requires a nonempty selection plus its manifest.

Names are relative slash-separated UTF-8 resource paths: no absolute paths,
colon, backslash, NUL, empty components, `.` or `..` components. Ordinary
dot-prefixed components are allowed. Resource identities follow
`lib/filesystem/ResourcePath.cpp`: recognized extensions determine type and are
removed from the case-insensitive ASCII name. Thus `image.png` and `IMAGE.BMP`
collide, but `image.png` and `image.json` do not. Unknown extensions remain part
of the name. Non-ASCII UTF-8 bytes are not case-folded.
Final-dot extension handling also applies to dot filenames: `.PNG` and `.BMP`
both have an empty IMAGE stem and collide. `.NHART` is a recognized
`ARCHIVE_NHART` extension; the packer does not automatically mount nested packs.
Distribution selections allow only IMAGE, ANIMATION, MASK, PALETTE, BMP_FONT,
TTF_FONT, JSON and TEXT resources. TEXT supports visual metadata/license text;
selection must remain explicit. Sound, executable/unknown files, scripts and
archives (including nested NHART or whole original archives) are rejected by
the distribution packer/verifier. The generic native container reader does not
impose this selection policy on other structurally valid containers.

## Embedded selection manifest

The exact reserved resource `.nhart/manifest.json` contains canonical UTF-8 JSON
and is not exposed as a runtime resource. Its resource identity is reserved from
user selections. It counts as an archive entry and has an ordinary index hash.
Its maximum payload size is 256 MiB. A package must contain at least one selected
resource in addition to this metadata.

The manifest has exactly `format` (integer 1), `requiredFamilies` (unique string
list), `requiredFamilyCounts` (family name to positive integer selected count),
and `entries`. Count keys must exactly match required families, and counts must
exactly match the selected inventory. Each selected entry has exactly these fields:

- `resource`: runtime resource path.
- `source`: relative file path under the explicitly supplied input root.
- `size`: exact byte length.
- `sha256`: lowercase 64-digit SHA256.
- `family`: inventory category, such as creature, town or UI.
- `origin`: portable provenance description, not a workstation path.
- `selection`: explicit reason/identity for selecting this version.
- `approval`: explicit review/rights status; the tool does not grant approval.

All required families must have a selected entry. Every archive payload must
match the embedded inventory's exact resource spelling, size and digest. Source
paths describe provenance but are not accessed during verification. Unknown
manifest fields, duplicate JSON members and nonfinite numbers are rejected.

## Maintained commands

```sh
python3 tools/nhart.py pack --manifest selected.json --input-root selected-inputs --output NewHorizons.nhart
python3 tools/nhart.py verify NewHorizons.nhart
python3 tools/nhart.py verify NewHorizons.nhart --manifest selected.json
python3 tools/nhart.py inspect NewHorizons.nhart
python3 -m unittest discover -s tools/tests -p test_nhart.py
```

Packing reads only explicitly selected files; it does not sweep directories.
Each source must resolve to a regular file within the input root, and its size
and hash must match before writing. Selected files and their relative directory
components must not be symlinks. Payloads and manifest entries are sorted by
UTF-8 resource name, JSON keys are sorted, and required families are sorted.
File mtimes and input-root locations cannot influence output bytes.

Output is staged in the destination directory, flushed, and independently
verified before atomic replacement. Invalid/missing inputs or failed staged
verification leave an existing package unchanged and remove the staging file.
No ordinary build step should generate a package from incomplete local inputs.
Verification and inspection need only the committed package, not artwork source
directories. Inspection verifies first and emits JSON metadata and provenance.
Optional `--manifest` additionally compares the embedded inventory against the
specified selected manifest after canonical ordering. The Python API is
`pack(manifest_dict, input_root, output)` and `verify(path, manifest=None)`;
the latter accepts an expected manifest dictionary or JSON filename.

Container validation is separate from decoder, mounting/precedence, rendered
presentation, licensing and clean-install player-package acceptance.
The native runtime reader checks structural bounds and resource identities but
does not recalculate payload hashes or require a selection manifest. Mandatory
Python verification during build/package validation establishes the inventory
and full-payload integrity boundary before a distribution pack is consumed.
