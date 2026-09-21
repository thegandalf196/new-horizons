# ONE original rendered Sorcery quality proof — NOT APPROVED

This tests the superseding miniature fantasy still-life brief. Neither motif nor
rendering quality has user approval. Rejected A/B and V2 remain unchanged. No
full-family work, style guide, live import, game launch or commit is claimed.

## Actual method and provenance

User explicitly authorized Blender installation and reported it installed. Artist
independently verified `blender --version`:5.0.1. This is actual CPU Cycles3D
rendering, NOT human painting, manual brushwork, an image-generation model or a
hired artist. Artist authored `scene.py`: original meshes/curves, rounded leather
board thickness, curved individual paper leaves, forged silver warding key,
modeled cloth folds, area lights, orthographic camera and material parameters.

Composition hypothesis: substantial open blue-leather volume, old silver key,
oxblood cloth on a warm dark table. Not an approved Sorcery motif. No images,
fonts, downloaded meshes/textures, purchaser pixels/compositions or historical
painting enters the scene. Sepia marks are authored short curves, not copied text.
Leather has subtle procedural micro-normal response; this is explicitly a shader,
not a claim that noise constitutes painterly craft. All new scene/code/art CC0-1.0
under `../LICENSE`; Blender/Pillow retain their own licensing. No original-art
rights asserted. All original scene inputs are self-contained and editable.

## Actual bounded execution

Build explicitly authorized ONE CPU2thread32sample768-square render with180s TERM
and10s kill grace. Exact command from repository root:

```
timeout --signal=TERM --kill-after=10s 180s blender --background --factory-startup --threads 2 --python assets/new-horizons/sorcery-art/still-life-proof-v3/scene.py -- --samples 32
python3 assets/new-horizons/sorcery-art/still-life-proof-v3/reduce.py
```

Actual Blender EXIT0, elapsed18seconds; reduction EXIT0. `pgrep -a -x blender`
returned no process afterward; slot released to Build before next compiler work.
Ignored evidence `research/sorcery-art/v3/render.log` and `exit.txt`. Blender log
contains use_nodes deprecation warnings for future6.0, not a failed render.
No rerender or second scene was run. Scene saves an editable .blend before render.
Fixed CPU/threads/sample count/seed731, AgX exposure.15, area lights and camera
are source-defined. Rerender byte-repeatability has NOT been tested; it cannot
be inferred from fixed settings or old concept repeatability.

## Privacy correction and review-safe files

Initial metadata audit correctly failed: Blender RAW `output/master.png` contains
machine-specific path metadata. Keep raw master, .blend and settings PRIVATE,
excluded via this directory's `.gitignore`, pending independent metadata review.
Do not distribute those files merely because rendering succeeded. Original raw
bytes remain preserved. Log likewise stays in ignored research.

`reduce.py` now constructs a fresh RGBA image without inherited metadata and saves
`output/master-review.png`. Verified pixel-for-pixel identical to raw render.
Native outputs were regenerated from the clean pixels (NOT another render).
All five review PNGs have empty metadata dictionaries; report
`research/sorcery-art/v3/image-audit.json`. `output/manifest.json` binds source and
output hashes; file presence in manifest is NOT publication permission for private
raw/source-scene files. Need independent source-scene privacy handling before
shipping editable .blend, retaining corresponding scene.py in any event.

Download/review ONLY:
- `output/master-review.png` —768x768 RGBA,
  SHA256 `92837278c0101bd83f6b727990c89d3f630572da787c7ee9796e77f3741c909f`.
- `output/native-review.png` —320x156 sheet with actual44/32/82x93 views,
  SHA256 `3bcfead9ef6ecbde1746fbdb0fe74a0f8b921ae95254bdf5b6ab97545056f45f`.
- `output/native-44.png`, `output/native-32.png`, `output/slot-82x93.png`.

The actual still-life has an opaque contextual table, intentional for this quality
proof; this is NOT a runtime transparent bookmark/header/emblem. Native reduction
uses Lanczos on premultiplied RGBa then returns straight RGBA. No aspect distortion
or artificial sharpening.82x93 slot uses82x82 centred with transparent top/bottom;
no rank or runtime resource is assigned yet.

## Actual visual inspection and open issues

Artist actually read768master and native sheet through image reader. Real rounded
volume, light/shadow, paper thickness, metal reflections and cloth depth are now
present, unlike the rejected flat approaches. Still a rendered study, not proven
painterly quality. Left page notation is mostly hidden: likely left-page normal/
solidify ordering needs correction in a later authorized revision. Some fine leaf
edge/metal shading is grainy at32samples; key-bit/shaft/contact needs refinement.
At32 the BOOK dominates and the key is small; Sorcery versus Knowledge semantics
are unproven. Pages/cloth could still look too clean/product-rendered and need
user judgment, not another unilateral batch. No claim all criteria are satisfied.

Next: Dispatcher presents clean master AND native views for actual user quality
choice/rejection. After explicit approval record exact accepted hashes, palette,
materials, light/edge/reduction rules; then complete all20 assets/ranks/states with
reproducibility, Frontend/Build import and sole Content composed in-game acceptance.
If user rejects quality/motif, address concrete feedback within a newly coordinated
bounded revision, preserving this proof. No full family before that decision.
