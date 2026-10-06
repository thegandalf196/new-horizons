# Academy large-portrait background mattes

Geometry-only art, with approval tracked per subject. Generated through the
built-in image tool and HoMM3 Art workflow; it contains no original colour pixels.

The Gremlin subject matte was generated through the built-in image tool and
HoMM3 Art workflow. Its first draft incorrectly included snowy architecture
and was rejected. The focused corrected draft in `masters/gremlin-matte.png`
was mechanically reduced with LANCZOS to58×64, converted to grayscale and
thresholded at128 to produce `mattes/gremlin.png`. Root inspected the private
native/8× before/after proof and accepted this corrected silhouette provisionally.
These files contain no purchaser-original colour pixels. Original-colour reference
and comparison images remain in ignored build outputs only.

Future runtime composition must use the purchaser's raw external TWCRPORT group0
frame30, preserving original creature pixels under this matte, over the existing
authored Academy desert backdrop. It must not use the original snowy scene as
a backdrop, commit original colour extracts, or modify the transparent CPRSMALL
cutouts. This asset alone is not completion of UP239 or user visual approval.

## Additional drafts, 2026-10-05

Each new master's exact prompt is in its adjacent `.prompt.txt`. The shared
exporter accepts `--creature` or `--all`; mechanical reproducibility does not
approve a silhouette. Private native/8× compositions remain ignored build output.

- `masterGremlin`: root inspected the composed native and8× portrait. It removes
  the blue spire, preserves the recognizable creature and is **Provisional**.
  It is installed through the runtime compositor.
- `stoneGargoyle`: **rejected draft**, not runtime art. The mask retains brown
  architecture below the left wing and pale snowy fringe beside the right wing.
  The focused v2 correction is preserved separately but still resembles the
  rejected geometry and is not selected for export or runtime.
- `obsidianGargoyle`: **rejected draft**, not runtime art. The left snowy building
  remains under the wing despite removal of the blue spire. Recognizability is
  not sufficient to approve incorrect background classification.

Both Gremlin variants have a verified runtime-compositor implementation.
The two Golem mattes pass root native/8× review provisionally and now have source
registration through the same compositor. Historical internal identifiers are
reversed: ironGolem ID32 uses frame34 (displayed Stone Golem), and stoneGolem
ID33 uses frame35 (displayed Iron Golem). Preserve those identifiers.
Mage and Arch Mage corrected mattes are reproducible drafts awaiting root native
composition approval; Genie and Master Genie masters are unexported drafts.
None of those four drafts is registered as a runtime portrait. Remaining portraits
must be reviewed independently; no complete-family approval is implied.

The runtime exporter `tools/export_new_horizons_academy_gremlin_portraits.py`
reads only authored sources: the binary masks and the Academy desert backdrop.
It crops the backdrop at `(0,10,100,120)`, reduces it to58×64 with LANCZOS and
copies the masks byte-for-byte. The large-icon fallback PNGs deliberately contain
only this backdrop, never purchaser creature pixels. In ordinary runtime,
AssetGenerator prefers the generated composition and reads the external raw
TWCRPORT frames30/31/34/35 through the masks. No small-icon overrides are installed.
Build and actual scaled-consumer verification are recorded separately in the queue.

## Exact correction prompt

Use case: precise-object-edit. Image1 is the authoritative enlarged 58x64 Gremlin portrait. Image2 is the faulty binary matte to correct. Return ONE pure white subject / pure black background segmentation matte, exact Image1 58:64 aspect ratio and framing. IMPORTANT correction: the tall blue pointed spire on the LEFT, from the top edge to just below half height, is BACKGROUND ARCHITECTURE, NOT a weapon or part of the Gremlin. Remove ALL that spire from the white matte; its silhouette must be BLACK. Only the Gremlin himself is white: pointed red cap, green face, both ears, hair, green/red clothing, arms, lower body, and the foreground hand/club below the left shoulder. Preserve the original exact pixel contour from Image1, including where lower-left forearm/hand meets background. Do not make holes through solid clothing or cut off the lower torso; black holes only where genuine background is visible between subject parts. No subject movement, pose changes, contour smoothing, thickening or inferred weapon. Matte only: pure black and pure white, no grayscale shading, no colored character, no label, no text, no panels or border. Match reference pixel geometry precisely; the result will mask the original creature pixels at native58x64.
