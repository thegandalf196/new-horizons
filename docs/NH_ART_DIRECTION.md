# New Horizons art direction and production

This is contributor guidance for producing and reviewing artwork. It requires no
art plugin. Approved project references and current asset contracts take precedence
over generic examples or historical commissions. It does not redefine gameplay.

## Read the relevant contract

- [UI style](NH_HOMM3_UI_STYLE_GUIDE.md): panel construction, controls, native-scale
  review, spell-symbol presentation, and creature motion.
- [Asset specifications](NH_ART_ASSET_SPECIFICATIONS.md): actual sizes, ranks,
  states, reserved regions, resource bindings, and complete family requirements.
- [Template composition](NH_TEMPLATE_ART_WORKFLOW.md): pinned native containers,
  foreground separation, protected borders, and template provenance.
- [School completeness](NH_SCHOOL_ART_COMPLETENESS.md): separate school and mastery
  slots; a complete family is more than a square skill illustration.
- [Runtime distribution](NHART_DELIVERY.md) and [NHART format](NHART_FORMAT.md):
  selected assets, manifests, packaging, installation, and verification.
- [Current Cabir reference](NH_CABIR_ART_REFERENCE.md): selected creature direction.

These documents already contain the project's more specific production rules.
Use the current-direction sections; old experimental requests and dated rejected
proofs are historical evidence, not new assignments.

## Visual language

Use late-1990s Western fantasy illustration with modeled physical volume,
selectively painterly edges, rich materials, dramatic light/shadow masses, and
saturated medieval colors. Steel, leather, parchment, feathers, bone, and cloth
should have visibly different behavior. Emphasize volume through value and light
rather than heavy cartoon outlines. Avoid photographic product-render polish,
plastic mobile-game gloss, flat vectors, neon flooding, and decorative clutter.

For secondary skills, compose a miniature fantasy still life: one dominant object
or coherent group, emphatic identifying shapes, a narrow safe inset, and little
wasted space. Small brilliant highlights and a strong silhouette must survive
native-size reduction. High-resolution art reduced to raster is the usual
production direction; intentional pixel clusters, dithering, and stepped outlines
are not a substitute for this visual language. Creature sprites need their own
native-scale treatment and coherent animation, not simply the skill-painting finish.

The illustration and its UI container are separate layers. A bare art study can
use an earthy contextual ground where appropriate. Product skill assets follow
the template-first contract: generate the foreground and compose it onto the
selected native template. Do not regenerate leather or border pixels per skill.
Spell emblems instead use identifying symbols with real transparency; their
parchment, spellbook page, and interface frame are separate surfaces.

## Asset-specific composition

### Secondary skills and school families

Derive the entire requested role/rank/state manifest before production. Preserve
accepted subject identity and make rank progression recognizable at small size.
For magic-school skills, the current project grammar is Basic scroll, Advanced
book, Expert orb, with school-specific effects and materials. This is a project
choice, not a universal rule for every skill or a historical attribution claim.

The existing skill contract includes 32x32, 44x44, 82x93, and 58x64 outputs for
each of three ranks. The school-plus-skill contract currently specifies 24 images,
including separate mastery borders, bookmarks, header, emblem, and button states.
Confirm the bindings before export; neither count covers individual spell art.
An explicit component-only request remains component-only.

### Artifacts and inventory objects

Make the object the focal subject. Preserve a complete, readable silhouette and
distinct materials; match the actual inventory background, angle, and slot.
Do not add an ornamental frame, halo, text, or glow by default. Confirm the asset's
consumer and transparency contract rather than applying secondary-skill sizes.

### Hero and creature portraits

Use distinctive anatomy, face, costume, armor, fabric, and hair under controlled
fantasy lighting. Preserve the selected identity during edits. Match the actual
portrait crop and aspect ratio; do not force every portrait into a skill square.
Portrait acceptance does not establish creature animation or map/battle sprites.
Use the faction's established creature-portrait background, referencing existing
portraits rather than substituting town-screen scenery. For background-only
edits, preserve the selected creature foreground and composition.

### UI, in-game bookmarks, and school headers

Build coherent outer frames, primary surfaces, recesses, content frames, controls,
and typography using the UI guide. Do not assemble arbitrary decorative cutouts.
Use the actual bookmark state and header contracts: game bookmarks are not printed
bookmarks, and a selected-school header is not an enlarged square skill icon.

Printed bookmarks or other commissioned illustrations, if separately requested,
may use narrative scenery and a requested border. Establish print dimensions,
safe margins, bleed, and layout separately; do not import that print workflow into
the runtime game-asset contract.

## Reference and revision handling

Inspect each supplied image before use and identify whether it is a style
reference, subject reference, or edit target. User-selected examples outrank
generic recollection. Preserve approved poses, composition, identity, materials,
and frames during a targeted correction unless a change is requested.

Define subject, role, materials, light/value arrangement, alpha/background,
dimensions, states, and constraints in a brief. Use examples selectively;
a previous commission is not a project-wide motif or a new assignment.

Use the host's available image-generation/editing capability when exposed. A role
or document does not provide a tool. The built-in route does not require a separate
image server or API key. If no suitable capability is available, report it and
provide the concrete brief or handoff; do not present a prompt, saved sample,
filesystem write, or mocked dispatch as newly generated artwork. Follow the host's
image tool requirements. Do not introduce another service or credential request
as an automatic workaround.

## Production and review

1. Inspect current approved references and source-backed slot contracts. Record
   the expected ranks/states, native dimensions, and template identities.
2. Generate or edit the original subject. For template composition, obtain a
   genuinely transparent foreground, not an opaque checkerboard preview. Preserve
   original returned bytes and verify actual dimensions and mode.
3. Inspect identity, silhouette, materials, light/shadow, clipping, unwanted text,
   and reference fidelity. A concrete correction preserves accepted parts; avoid
   repeated speculative variants or extraction loops. The approved workflow allows
   one initial attempt and at most one targeted correction per request before
   reporting an unresolved production limitation.
4. Compose each product slot at its exact native size. Resize only the foreground
   with premultiplied-alpha filtering and explicit placement; preserve protected
   template pixels. Do not stretch a completed square icon into a portrait/header.
5. Inspect native-size samples, nearest-neighbor enlarged samples, alpha edges over
   black/white/brown backgrounds, rank/state differences, and the whole integrated
   UI. Revise the source when legibility fails; do not hide defects with arbitrary
   sharpening, new outlines, or excessive glow.
6. Deliver actual files and viewable previews for review. Distinguish technical
   export success, visual approval, integration, and in-game verification. Approve
   a representative direction before expanding the full coherent requested family.

## Export, preservation, and provenance

Keep authoring masters, rejected variants, reference images, review grids, exact
prompts, raw tool metadata, and execution receipts in configured private storage.
Use new versioned output directories. Do not overwrite an existing review or
silently crop, stretch, flatten alpha, or remove an accepted frame. Preserve the
source master and record source/output hashes, actual dimensions, processing,
rank/state names, and generation/reference provenance in a private manifest.

For a standalone square study, ordinary LANCZOS reduction into 44x44 and 32x32
PNGs can be useful for review. This is not the complete product delivery contract.
Non-square padding requires an explicit contain/padding decision; portraits,
headers, and print work preserve their requested aspect ratios. Verify alpha and
dimensions. PNG export alone does not produce game-ready DEF/PCX, indexed palettes,
animation frames, print specifications, or correct runtime bindings.

If Python/Pillow tooling is used, resolve the environment's absolute interpreter
and use an isolated dependency environment when necessary. Do not embed a personal
workstation path in public docs or modify another project's environment to install
dependencies. A generic square-icon exporter is suitable only for standalone
studies; it is not the product's native-template compositor.

Public asset records contain portable resource identities, selected-file hashes,
origin, applicable attribution, review state, and technical contracts. Do not
publish private prompt/receipt paths or copied image metadata automatically.
An unchanged master can retain identifying metadata: preserve it privately and
prepare a reviewed distribution derivative where needed. Neither a source hash,
tool-sidecar record, nor approval alone establishes third-party distribution rights.
Follow existing selected-file provenance and license records; do not relabel a
template composite as independently authored or erase third-party attribution.

Publish selected runtime resources through NewHorizons.nhart and its reviewed
manifest. Use the committed pack during normal builds without private overlays or
loose-art fallbacks. Test distribution separately from authoring and local previews.
An explicitly selected provisional draft may ship without being relabeled Final;
selection, visual approval and rendered acceptance remain separate records.
