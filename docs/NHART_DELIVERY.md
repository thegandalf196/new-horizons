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

Retained loose exports are development/reference material under
`assets/new-horizons/runtime-development-inputs`, not mounted or installed.
They are not a complete set of selected authoring inputs. Their previous copies
remain in Git history. Review files and authoring masters have not been removed.

## Maintenance

Prepare an explicit input staging root containing the exact source-relative
paths in the manifest. New artwork updates require reviewed inputs; a normal
build must not regenerate the pack from an incomplete local collection.

```sh
python3 tools/nhart.py pack --manifest assets/new-horizons/runtime-art-manifest.json --input-root selected-inputs --output Mods/new-horizons/NewHorizons.nhart
python3 tools/nhart.py inspect Mods/new-horizons/NewHorizons.nhart
python3 tools/nhart.py verify Mods/new-horizons/NewHorizons.nhart --manifest assets/new-horizons/runtime-art-manifest.json
python3 tools/verify_new_horizons_art_install.py --resources installed-data --manifest assets/new-horizons/runtime-art-manifest.json
```

See [NHART_FORMAT.md](NHART_FORMAT.md) for byte fields and tool contracts.
Verification of a committed distribution needs no handoffs or authoring inputs.
The packer stages and verifies its output before replacement.

Builtin bootstrap and module scope mount the same full-path pack. Explicit
overlay ordering gives builtin custom art precedence over original archives;
the module mounts its pack after readable Content configuration. No alternate
renderer or image format is introduced. Per-entry independent bounded streams
preserve the existing decoded-image/animation caching policy.

## Acceptance boundaries

Container integrity, source compilation, resource/decoder integration, rendered
clean-install acceptance, performance comparison and downloadable release
integrity are separate gates. See the user-priority queue for actual progress;
these instructions do not claim completion of unexecuted gates.

Performance comparisons must use identical payload bytes and distinguish
index/loading costs from decoding/drawing. Fresh processes are not proof of a
cold OS cache. No system cache flushing or system-wide setting changes are
required or permitted by this workflow.
