# Selected runtime artwork delivery

The distribution resource is `Mods/new-horizons/NewHorizons.nhart`.
`assets/new-horizons/runtime-art-manifest.json` identifies selected bytes,
virtual resource identities, source staging paths, provenance, approval and
required family counts. The pack is genuine NHART v1, not ZIP; its existing
PNG/DEF/PCX/JSON payloads are consumed by the normal engine resource system.

The current selected presentation includes Cabir/Master Cabir animations,
map sprites, portraits and projectiles; turbaned Mage/Arch Mage resources;
Wisps; Academy and Mage Guild artwork; school/spell/casting effects; and custom
skill/perk/Order/interface resources. Original-based modifications and finished
composites are intentionally included. Selection and provisional visual
approval are distinct from origin and licensing. No blanket independent-art
ownership or third-party license grant is asserted.

Unchanged original resources remain external. Original archives, executable
files, rejected alternatives, comparison sheets and authoring masters are not
runtime container inputs. Existing source/provenance/license records remain
applicable. Earlier private-only import classifications do not remove selected
Cabirs, turbaned Magi, Guild composites or interface resources from this pack.

Selected runtime artwork is published through `NewHorizons.nhart`.
Loose authoring files, runtime staging inputs, alternatives and review materials
are maintained locally and are not committed by default. The public repository
retains the package, tools, manifests and records. Calling an export reference,
development-only or superseded is not permission to publish it as a loose image.
This includes deliberate original-based modifications and finished composites.

The supplied application icon has a specific platform-bootstrap exception:
`clientapp/icons/new-horizons.ico` is embedded in the Windows executable, and
the nine `new-horizons.<size>x<size>.png` derivatives serve Linux desktop lookup
before the engine mounts NHART. Their hashes, conversion and authorization are
recorded in `assets/new-horizons/platform-icon-provenance.json`, retained with
the package notices. The source master and review previews remain local; this
exception does not permit loose runtime-art collections.

Before removing tracked artwork, preserve and SHA256-verify recoverable copies
outside the repository. Keep the local input root explicitly configurable; do
not depend on a particular workstation path. Pulling an artwork-removal commit
can remove the tracked files from another worktree: back up its local work first.
Existing Git history, other branches, tags and releases can still contain older
loose copies. This current-tree policy does not erase those copies or prevent
extraction from the package.

## Maintenance

Prepare an explicit input staging root containing the exact source-relative
paths in the manifest. New artwork updates require reviewed inputs; a normal
build must not regenerate the pack from an incomplete local collection.
The manifest's `source` paths describe authoring/import inputs, not files that
must exist in a fresh checkout. Read selected resource bytes through NHART when
testing delivery. Use synthetic images when testing an authoring algorithm.
Generation and review workflows must use an explicit local output root rather
than recreate loose artwork in publicly tracked directories.

```sh
python3 tools/nhart.py pack --manifest assets/new-horizons/runtime-art-manifest.json --input-root "$NH_ART_INPUT_ROOT" --output Mods/new-horizons/NewHorizons.nhart
python3 tools/nhart.py inspect Mods/new-horizons/NewHorizons.nhart
python3 tools/nhart.py verify Mods/new-horizons/NewHorizons.nhart --manifest assets/new-horizons/runtime-art-manifest.json
python3 tools/verify_new_horizons_art_install.py --resources installed-data --manifest assets/new-horizons/runtime-art-manifest.json
```

See [NHART_FORMAT.md](NHART_FORMAT.md) for byte fields and tool contracts.
Verification of a committed distribution needs no handoffs or authoring inputs.
The packer stages and verifies its output before replacement.
For repacking, set `NH_ART_INPUT_ROOT` to the verified external staging directory;
do not use this command during an ordinary build or with incomplete inputs.

Builtin bootstrap and module scope mount the same full-path pack. Explicit
overlay ordering gives builtin custom art precedence over original archives;
the module mounts its pack after readable Content configuration. No alternate
renderer or image format is introduced. Per-entry independent bounded streams
preserve the existing decoded-image/animation caching policy.

### Installed artwork preparation

NHART remains the public distribution authority; installing prepared resources
does not authorize committing loose artwork. Before gameplay, the native
resource loader prepares a verified, package-digest-keyed directory in the
current profile's cache. Both mounts retain their original resource identities
and precedence, but read prepared files after preparation completes. There is
no per-frame extraction, Python dependency for players, or silent archive
fallback. Preparation preserves payload bytes, including animation descriptors
and frame offsets; it does not regenerate or resize artwork.

The first preparation requires writable cache space. Subsequent launches verify
the prepared inventory before reuse. A damaged or unsafe cache produces an
actionable startup error rather than unverified artwork. Other package versions
and user saves are not removed. Keep the shipped NHART file: the cache is not a
replacement distribution, and ordinary builds still use the committed package.

Storage preparation and rendering performance are separate concerns. In
particular, replacing archive reads does not by itself fix SDL texture uploads
or image decoding; improvements must be measured rather than assumed.

The native startup cache currently admits packages up to 1 GiB. A crashed
preparation can leave a `<package-digest>.lock` or a digest-prefixed staging
directory under `new-horizons-art/v1`. With all game processes closed, recover
only the identified generated cache entries, retaining the shipped package;
do not delete a whole profile. Failed staging directories are retained rather
than recursively deleting potentially substituted paths. Cache integrity is
checked at startup, not continuously against later same-user modifications.

## Acceptance boundaries

Container integrity, source compilation, resource/decoder integration, rendered
clean-install acceptance, performance comparison and downloadable release
integrity are separate gates. See the user-priority queue for actual progress;
these instructions do not claim completion of unexecuted gates.

Performance comparisons must use identical payload bytes and distinguish
index/loading costs from decoding/drawing. Fresh processes are not proof of a
cold OS cache. No system cache flushing or system-wide setting changes are
required or permitted by this workflow.
