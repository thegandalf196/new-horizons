# Cabir animation drafts — 2026-10-06

Original artwork created with built-in image generation through HoMM3 Art.
References are this project's original v2 standing master or an earlier generated
candidate, not extracted purchaser sprites. Source masters remain unchanged.
The user explicitly approved manual transparency cleanup and alignment; that
permission does not authorize synthetic pose warping. These are provisional
offline assets, not a completed creature or a change to the normal launcher.

## Walking

`walk-v2/candidate-02.png` has four distinct row-major gait poses. The reviewed
cleanup preserves every RGB byte and all retained RGBA, clears only external
alpha at most10, and records22,851 cleared pixels. Original SHA256:
`06ba5ad74fdb2436337b175346ce3bc63e048b07ba04c6c006a2378750f9754a`.
`walk-v2/cleaned/cleanup-receipt.json` records the complete policy and counts.
The native PNGs share a450x400 canvas, one common scale/placement and y268
ground baseline. Four-frame GIF and4x contact sheet are review derivatives.
Root and independent review permit this provisional offline checkpoint. Native
animated smoothness and appearance on battlefield backgrounds remain unverified.
No production descriptors were installed. Earlier rejected walk-v1 sheets remain.

## Other poses

Melee-front `melee-v1/candidate-01.png` has four distinct silhouettes (ready,
windup, thrust, recovery). The thrust crosses the nominal grid column; grid
cropping would destroy part of the vessel. Use separate silhouette extraction.
SHA256: `148b3a365e5456c72935ca280afb6e36db61f5f0f3d0be395145d16c5403a257`.

Reaction `reactions-v1/candidate-02.png` has hit, brace, dying and dead poses.
Alpha inspection confirms a transparent cutout, not an opaque backdrop. Upper
feet cross the nominal row boundary by38pixels; grid cropping is invalid.
Candidate01 is retained;02 reduces exterior alpha fringe. SHA256:
`744691e95d2bcfcb0ba7632ed158f9c24f1ad021edaba9eb13cd78b5dec49349`.

`tools/extract_new_horizons_cabir_poses.py` separates these two pinned sheets
into four whole silhouettes each, without quadrant cropping. `separated/` holds
original-scale archival PNGs on a common source-space canvas and per-pose global
offsets in `separation.json`. This is NOT native battle alignment. Reviewed
dilation4 discards only faint external alpha: melee18,609/max5; reactions7,473/max6.
Surviving RGBA is byte-preserved, originals/hash unchanged, independent resizing
absent. Canvases are1168x1108 and1463x946 respectively. Synthetic checks cover
cross-quadrant preservation, missing/merged components and meaningful-alpha
rejection; actual deterministic re-export matches receipt hashes.

`death-v1/candidate-01.png` is a separately authored lying-dead pose, not a
rescaled or rotated standing sprite. Its exact generation prompt is in
`death-v1/dead.prompt.txt`. Cross-action physical scale and alignment need review.
SHA256: `b8347fdb55c6f7abe880af9cb1daecdf311caea607aed3c381bb2de556dae1a2`.

Upgraded standing draft lives at `../../cabir-master/v2/candidate-01.png`:
same anatomy, with an iron shoulder guard and reinforced vessel. It is a new
provisional candidate, not user-approved final art. SHA256:
`f4e61db6c57587e7f66c43987ee5a0c49ffc494c9e9e3916add6101b0977f45c`.

## Exact built-in prompts for this checkpoint

The walk and melee prompts are retained beside their masters. The following
record the exact subsequent calls (all requested transparent_background=true).

Reaction candidate01, reference `standing-master.png`:

> Create an original transparent 2 by 2 creature reaction/death atlas using this image as exact Cabir identity/material/style reference. SAME right-facing squat charcoal reptilian fire imp, orange fissures/eye, brass collar/cuffs, cream trousers, burgundy hanging cloth, claw feet and brass fire vessel. Dark crude late-1990s Heroes III pre-rendered fantasy, reduced detail modeled forms, not polished mobile illustration. Four distinct equal cells in row-major order: top left hit reaction shoulders recoiling with planted feet and pot still held; top right defensive brace knees bent pot drawn close; bottom left dying, body collapsing sideways bent legs head lowered vessel falling beside hands; bottom right DEAD, body fully lying low on its side, eye dark, vessel lying beside hands, flame extinguished. Truly collapsed body, not standing figure tilted. Same body proportions, camera and scale in all cells; consistent ground baseline. Generous transparent margins on ALL cell edges including enough horizontal room for lying body. No overlap between cells. NO background, shadow, detached particles, captions, grid lines, extra anatomy or invented gear. Transparent background.

Reaction candidate02, edit target `reactions-v1/candidate-01.png`:

> Edit ONLY background: completely remove brown/black ambient glow backdrop and haze around all four figures. Actual transparent alpha outside creatures and vessels, NOT black background or painted checkerboard. Preserve all four Cabir poses, materials, body proportions, camera, scale and exact 2x2 layout. Keep flame inside vessels in first three poses, dead vessel extinguished. No detached particles. All figures separated from cell edges by transparent margins. No new poses or gear, no text. Preserve creature artwork while removing background.

Upgraded standing candidate01, reference `standing-master.png`:

> Create ORIGINAL Cabir Master upgraded standing sprite using reference as exact identity/pose/style. Preserve squat charcoal reptilian fire imp anatomy and proportions, right-facing three-quarter pose, dark skin orange fissures and eye, cream trousers, burgundy cloth, claw feet. Upgrade ONLY equipment: one dark worn iron shoulder guard, thicker iron-reinforced brass fire vessel with simple projecting spout held in same two hands; worn brass collar and cuffs. Still dark crude reduced-detail Heroes III late-1990s modeled creature sprite legible at 60 pixels tall, NOT taller or glossy cute mobile art. Actual TRANSPARENT background. Full unclipped figure, clear transparent margin. NO backdrop, ground shadow, halo, floating embers, text, border, new weapon or extra limbs. Flame ONLY in vessel mouth. Same standing design apart from upgraded equipment.

## Remaining production scope

Additional original candidates: `melee-directions-v1/candidate-01.png` authors
separate upward/downward windup and impact trajectories; exact built-in prompt
is beside it. `../../cabir-master/v2/shoot-front-v1/candidate-01.png` authors
ready/aim/release/recovery for horizontal ranged attacks, using the new upgraded
standing master as identity reference. Its exact built-in prompt is beside it.
Both remain unaligned drafts awaiting alpha/geometry and native-size review.

These original generated assets follow the project's original-art CC0 dedication
in `assets/new-horizons/README.md`; no rights in purchaser assets or another
Heroes game's Cabir depiction are asserted. Tools retain the project source license.

Cross-action scale/pivots, directional melee, upgraded movement/reactions,
directional ranged attacks and projectile timing, portraits, army/adventure
icons, animation descriptors, resource validation/build and playable delivery.
Keep IDs28/29 and existing gameplay/sounds. Required groups are movement,
holding, hit, defence, death and three directional melee attacks; upgraded
creature also needs three directional shooting groups. Optional transitions
must not be simulated by mapping every group to the same standing sprite.
