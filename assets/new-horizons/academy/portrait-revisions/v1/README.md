# Academy large-portrait background mattes

Latest source: Titan v3 provisionally accepted and runtime-integrated, ID41/frame43.
Its recognizable anatomy and lightning pass native review; five isolated contour
uncertainties are deferred aesthetic polish rather than Phase1 blockers. All
twelve runtime portraits pass the actual scale1–4 consumer. Gargoyles remain
unregistered: Stone v3 fails to remove the left building; Obsidian v2 retains
that building and v3 invents upper-left geometry/internal holes. Failed drafts
and exact prompts are preserved, not approved exports. No complete-family or
Final-art claim is made. Earlier dispositions below are historical checkpoints.

Giant is now runtime-integrated (ID40/frame42), provisionally accepted. Its
upper-left blue shape in comparisons belongs to the authored backdrop, not
retained original Tower scenery. Eleven-portrait actual scaled-consumer/build
checks pass; latest promoted playable remains ten portraits.

Titan focused v2 fills two torso holes; v3 fills the left hand slit. Both exact
prompts and old sources are preserved. V3 additionally fills sky pixel(6,15)
and changes four contour pixels, so it remains unapproved and unregistered.
Separate `titanV2`/`titanV3` mechanical exports do not select runtime artwork.

Delivery checkpoint: committed source `6e600fc89` supplies ten provisional
runtime portraits through the promoted Linux candidate. Native scale1–4
consumer, build and bounded headless map/AI checks pass. Four subjects are
still unregistered; no complete-family or user visual approval is implied.

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
Mage and Arch Mage corrected mattes pass root native/8× composition review
provisionally. Independent pixel comparison confirms the previews use the current
masks and runtime backdrop: the narrow upper-left spire visible after composition
belongs to the authored desert scene, not retained Tower snow. Both now have
runtime source registration, with verification tracked in the queue.
Genie and Master Genie now have provisionally approved runtime registrations.
Genie's head opening is genuine sky between the curled plume/cap; its pale sweep
is the connected foreground weapon, not snow. Master Genie uses the selected v2
mask described below. All eight portraits remain Provisional, not Final. Remaining
portraits must be reviewed independently; no complete-family approval is implied.

Master Genie focused revision: the initial matte incorrectly masks six native
pixels of red/gold clothing at x45–47,y35–37 as background. The built-in-generated
`masters/masterGenie-matte-v2.png` fills that isolated hole; its exact targeted
prompt is retained alongside it. Mechanical reduction also changes two silhouette
edge pixels (43,13 and4,26), so review the native composite before selecting it.
Root inspected the native/8× composite and accepted v2 provisionally; its two
minor edge changes are not visually prominent. The exporter and runtime use v2,
while the rejected prior master/mask remain preserved unchanged.

Earlier matte-review checkpoint: the Naga draft passed native/8× review but was
not yet registered. The first Naga Queen draft was rejected for internal holes
through subject shading, including the enclosed x23–27,y48–53 patch. That Queen
master and matte remain preserved; the focused v2 correction below supersedes
the initial rejection for the selected export, not the preserved v1 files.

Ten-portrait integration checkpoint, 2026-10-05: Naga (core ID38, external
TWCRPORT frame40) and Naga Queen (ID39, frame41) are provisionally accepted and
registered through the production compositor. Queen's selected
`masters/nagaQueen-matte-v2.png` reduces to `mattes/nagaQueen-v2.png` (SHA256
`aa282c71f59b720ccc2604e81b3bc1527b8e58b7ab60403eee9fb28eb9f64c30`); the v1
master and mask remain unchanged. The ten-portrait native consumer passes at
scales1–4 with zero skips in
`build/new-horizons-linux/Testing/Temporary/LastTest.log`; client and fixture
builds passed in the prior cycle. This is art/runtime source coverage, not
gameplay identity credit or a new playable delivery.

The remaining four subjects are not all approved: Stone and Obsidian Gargoyle
drafts are rejected; Giant is provisionally accepted but not registered; Titan
remains unapproved pending review of interior-looking holes near the hand/bolt
junction and right beard/neck/armor. Mechanical `--all --check` validates
reduction reproducibility only; it does not certify every matte as suitable art.

The runtime exporter `tools/export_new_horizons_academy_gremlin_portraits.py`
reads only authored sources: the binary masks and the Academy desert backdrop.
It crops the backdrop at `(0,10,100,120)`, reduces it to58×64 with LANCZOS and
copies the masks byte-for-byte. The large-icon fallback PNGs deliberately contain
only this backdrop, never purchaser creature pixels. In ordinary runtime,
AssetGenerator prefers the generated composition and reads the external raw
TWCRPORT frames30/31/34/35/36/37/38/39/40/41 through the masks. No small-icon
overrides are installed.
Build and actual scaled-consumer verification are recorded separately in the queue.

## Exact correction prompt

Use case: precise-object-edit. Image1 is the authoritative enlarged 58x64 Gremlin portrait. Image2 is the faulty binary matte to correct. Return ONE pure white subject / pure black background segmentation matte, exact Image1 58:64 aspect ratio and framing. IMPORTANT correction: the tall blue pointed spire on the LEFT, from the top edge to just below half height, is BACKGROUND ARCHITECTURE, NOT a weapon or part of the Gremlin. Remove ALL that spire from the white matte; its silhouette must be BLACK. Only the Gremlin himself is white: pointed red cap, green face, both ears, hair, green/red clothing, arms, lower body, and the foreground hand/club below the left shoulder. Preserve the original exact pixel contour from Image1, including where lower-left forearm/hand meets background. Do not make holes through solid clothing or cut off the lower torso; black holes only where genuine background is visible between subject parts. No subject movement, pose changes, contour smoothing, thickening or inferred weapon. Matte only: pure black and pure white, no grayscale shading, no colored character, no label, no text, no panels or border. Match reference pixel geometry precisely; the result will mask the original creature pixels at native58x64.
