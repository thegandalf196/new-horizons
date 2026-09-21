# Template-first native compositor — private technical work, NOT artwork approval

User selected actual modder-tools-pack blank templates. This supersedes all
background-color03 generation requests. External editor must supply original
armillary FOREGROUND03 with true RGBA/clean holes/wisps; no local threshold cutout,
new Blender/SVG proof or generated UI/leather substitute. User submits the prompt
in external HoMM3-art/TEMPLATE_FIRST_WORKFLOW.md; no automatic transport/monitor.

## Pinned native placement contract

Read actual inventory/manifest/resource readme and native32/44/82/header images.
placements.json binds three selected blanks to repository commit
`e21c90192cb2faf2c4d41347141248e19283a5ce`, exact Git blob/SHA256/dimensions/mode.
No template bytes included in this directory. Default foreground fitting rectangles
are32:[2,2,30,30],44:[2,2,42,42],82x93:[3,3,79,90], exclusive right/bottom.
These conservative protected-border defaults are NOT approved visual placement.
Native templates are never resized/recolored. Full foreground canvas (not alpha
bbox crop) fits each rectangle uniformly and centres within it; explicit resulting
scale/dimensions/offset and actual alpha mask bounds enter each manifest. An82x93
container remains82x93, not a stretched square. Tune only with actual alpha/user
review, preserving input and reviewed output revisions.

School header160x96/top28, scenario58x64, emblem/buttons and80x60 bookmarks remain
separate future contracts. spellBookBlank67x48 is NOT a school bookmark. No full
rank progression before chosen subject approval. Runtime filenames/states remain
owned by Frontend/Build, not by this private compositing experiment.

## Actual software method

compose.py accepts externally supplied RGBA plus exact SHA256 and sidecar. It
rejects RGB/checkerboard-as-background, all-opaque/empty/hard-cutout-only alpha,
missing partial-edge samples and nontransparent canvas rim. These checks detect
structural failures, NOT whether ring holes/wisps/anatomy were faithfully isolated;
actual master/alpha-channel pixels must still be viewed. No background removal,
recoloring, alpha threshold, painting, sharpening or new image generation.

Pillow full-foreground premultipliedRGBa Lanczos -> straightRGBA. Overlay composited
once with alpha_composite over exact native template. Every pixel outside actual
foreground alpha mask is bit-for-bit templateRGBA; protected-border values and
whole template alpha channel unchanged. A foreground/template-alpha conflict
REJECTS instead of silently clipping away meaningful content. No added synthetic
shadow: supplied foreground may include a properly authored shadow layer later.

Input hash/blob/size/mode guards prevent a scaled substitute. Output directories
must be new revisions under ignored owned research; no overwrite of old reviews.
Input foreground/sidecar/placements/templates rehashed unchanged. Layer PNGs and
manifest retain explicit input identity and placement for editable recomposition.
PNG chunk/CRC/trailer audit retains original metadata report but clean outputs
allow only IHDR/IDAT/IEND. caBX presence is not verified generator history/rights.

## Tests and scheduling

Build granted LIGHT SOURCE/planning ONLY at preparation. No compositor run or
synthetic tests have executed yet; separate bounded slot requested. test_compose.py
uses ONLY synthetic software fixtures, no extracted original/reference assets or
art-generation claim. Covers15 cases: RGBA/partial-alpha/rim rejection, exact single
alpha application, premultiplication fringe prevention, protected border/zero-mask
pixels/input immutability, templatealpha conflict rejection, safe rect/aspect,
repeat PNG bytes, hash/blob/size/mode substitution and badCRC rejection.

After explicit Build test-slot ACK:
`python3 assets/new-horizons/sorcery-art/template-compositor/test_compose.py -v`

After real foreground arrival, visual separation check, and separate Build review
slot, proposed command (paths supplied externally, never bundled here):

```
python3 assets/new-horizons/sorcery-art/template-compositor/compose.py \
  --foreground <actual-RGBA-foreground03.png> --foreground-sha256 <verified-sha> \
  --provenance <actual-sidecar.md> --templates <private-pinned-template-directory> \
  --out build/new-horizons-linux/research/sorcery-art/template-review-01
```

No placeholder substitute can satisfy the external-art gate. Synthetic tests are
technical software proof only, not user/native/in-game acceptance or release art.

## Licensing boundary and ownership

Only newly authored SOFTWARE/placement notes here are CC0-1.0. Composites are NOT
CC0 merely because the compositor is. Upstream mod.json explicitly declares CC
BY-SA4.0, authorVarious; GitHub null license field is not absence of declaration.
Readme mentions Complete/HotA/game-derived backgrounds. This establishes mixed
origin concerns, not blanket invalidity or blanket clearance. Content/Build must
classify EACH selected file and attribution/ShareAlike/dependency obligations
before shipping. External foreground provenance/tool terms require separate review.
Private composition/use approval does not grant redistribution rights. Do not
ship these templates/composites until resolved. Original-alpha-only distribution
with lawful installed-resource composition is an option for Frontend investigation,
not authorized runtime rewrite or bypass of current asset loaders.

No original references/blank PNGs/live output/product config/shared generator edits,
GUI/game launch or commits by Artist. Preserve all previous candidates/proofs.

## Actual synthetic checkpoint (supersedes preparation-only test status above)

Build reviewed source and granted ONE<=10s synthetic-only slot. Actual command:
`PYTHONDONTWRITEBYTECODE=1 timeout 10s python3 assets/new-horizons/sorcery-art/template-compositor/test_compose.py -v`.
EXIT0,15PASS,0.020s. Preserved argv/exit/full warnings and exact tested source hashes
in ignored `research/sorcery-art/template-compositor-tests/`. Tested compose hash
wasd5a1fc10e313b2e635b0239cb566fd7033ed33d8b8b75b260acbf1c2106516b5;
test hashbac83a8f9697fa10abe8edcf758c610d7dbfb553e962c75c9b2db251150fb417.
No CLI main, actual templates, purchaser pixels or supplied-art composites run.
LargeRGB templates normalize toRGBA output: pixel/alpha behavior, NOT preservation
of the original PNG color mode. No art/rights/actual-alpha quality gate passed here.

Warnings retained: Pillow getdata deprecation and ResourceWarning on the negative
template size/mode branch. Subsequently changed load_template to context-manage
Image.open so failure closes its handle. This narrow resource-lifetime source fix
has NOT been rerun; initial15PASS binds the recorded earlier source, not an invented
post-fix test. No additional batch after slot release. Future approved test/intake
slot should rerun15 before supplied-art composition; deprecation is future-Pillow
maintenance, not a currently observed output failure. Actual alpha03 still pending.

Later Build explicitly authorized the context-manager repair retest with the same
bounded command, distinct `template-compositor-tests-contextfix/` evidence. Actual
EXIT0,15PASS in0.017s; ResourceWarning absent, getdata deprecations retained. Tested
current compose SHA256f52ece5a603620fe385ef8efad5ab801159087cc730901b88e70cb1af956b70e.
Old warning report/source hashes preserved; slot released, no more execution.
Current synthetic source now has actual retest evidence, still no real-art/native
composition, legal clearance or user approval. No further source changes implied.
