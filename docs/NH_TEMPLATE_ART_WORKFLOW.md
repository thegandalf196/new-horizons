# Template-first artwork pipeline

**Current process:** [NH_APPROVED_ART_WORKFLOW.md](NH_APPROVED_ART_WORKFLOW.md)
records the successful native-RGBA spindle experiment and explicit user approval
of the automatic workflow. Preserve the inventory, composition and rights rules
below, but treat the old foreground03 armillary extraction task as historical.
No manual masking from the user, no new extraction loop, no final motif/import
approval inferred from process acceptance.

## User decision

User identified https://github.com/vcmi-mods/modder-tools-pack and explicitly
requested integrating its templates into the workflow, notably the actual skill
leather backgrounds. Stop spending generation attempts approximating a UI surface
that already has a template. The skill illustration and its UI container are
separate layers. This supersedes Sorcery revision03 background-color generation.
No existing draft is final-approved merely because a template has been found.

## Verified inventory, not inferred from repository name

Inspected complete non-truncated tree at commit
`e21c90192cb2faf2c4d41347141248e19283a5ce` (main at inspection).
Downloaded only17 blank PNGs plus manifest/readme for private local inspection;
verified each PNG's Git blob hash and recorded SHA256/dimensions/mode. Did not
clone/install the pack, execute bundled tools or enable it as a gameplay mod.
Inspection files remain in the private art reference workspace, outside this
repository.

Native files under upstream
`modder-tools-pack/content/Heroes III Modding Resources/blankIcons/`:

| UI role | Template | Dimensions |
|---|---|---|
| Small secondary skill | secondarySkillSmallBlank.png |32x32 RGBA|
| Medium secondary skill | secondarySkillBlank.png |44x44 RGBA|
| Large secondary skill | secondarySkillLargeBlank.png |82x93 RGB|
| School header | spellSchoolBlank.png |160x96 RGBA|
| Spellbook illustration container | spellBookBlank.png |67x48 RGBA|
| Spell effect | spellEffectBlank.png |48x36 RGB|
| Scenario/bonus | bonusBlank.png / bonusSpellBlank.png |58x64 RGBA|
| Creature bonus | creatureBonusBlank.png |51x51 RGBA|
| Specialty | specialtyBlank.png / specialtySmallBlank.png |44x44 /32x32 RGBA|
| Scroll | spellScrollBlank.png / spellScrollSmallBlank.png |83x61 /43x34 RGBA|
| Spell specialty | spellSpecialtyBigBlank.png / spellSpecialtySmallBlank.png |44x44 /32x32 RGB|
| Town build icon | townBuiltLarge.png / townBuiltSmall.png |58x64 /48x32 RGBA|

Dispatcher viewed actual native templates: the three skill sizes have different
brown leather fields/borders, not one interchangeable high-res brown swatch.
School header has a parchment field and transparent upper region. Native-size
compositing is essential; do not scale the32px template up to manufacture82px.
Other upstream folders include XCF adventure-button/logo templates, mage guild,
projectile, town/hall, puzzle and creature-position resources. These are catalog
leads, not yet reviewed/approved imports. A67x48 spellBookBlank is not automatically
a custom school bookmark; inspect the runtime role before assigning any template.

## Licensing and provenance

GitHub's API license field is null, but that is NOT the full evidence: upstream
mod.json explicitly declares Creative Commons Attribution-ShareAlike and links
CC BY-SA4.0, with author `Various`. The resource readme invites use in mods but
also describes Complete/HotA resources and some backgrounds reconstructed from
game imagery. The blanket declaration cannot independently establish rights to
all underlying pixels. Preserve both facts; neither claim 'no license exists'
nor 'everything is cleared'.

Use the downloaded blanks for private reference/compositing review now. Before
shipping selected templates/composites, Content and Build must record per-file
origin, applicable license/attribution and any inherited game-content dependency.
Do not call a composite CC0 if it includes CC BY-SA or proprietary template pixels.
Where rights are established under CC BY-SA, retain attribution, license link,
modification notes and applicable ShareAlike terms for adapted art; do not assume
a global code-license change from an art asset. Where a template is purchaser-
derived and cannot be redistributed, investigate supplying only original alpha
foregrounds and compositing against legitimate installed assets through existing
runtime resource hooks. No blanket public-release/legal assurance or invasive
runtime rewrite follows automatically. Unclear cases remain excluded from public
packages until resolved; independent original template creation remains a fallback.

## Practical production route

1. **Catalog and select:** match each actual UI slot to a pinned template, including
   dimensions, alpha/margins, frame/state semantics and reserved borders. Retain
   hashes and evidence. Keep unrelated pack content/executables out of the product.
2. **Generate foreground, not UI:** external image editor creates original subject
   art without background, frame, leather, rank badges or rendered UI. Prefer true
   RGBA with clean partial alpha; provide a separate matte if supported. Preserve
   the master/edit provenance. No checkerboard painted into an opaque PNG.
3. **Verify separation:** real alpha/edge inspection, holes between rings, magic
   wisps and contact shadow need review. Do not rough-threshold the flattened
   opaque Sorcery master to obtain a damaged cutout. If needed, return to the
   image-capable editor for separation rather than recoloring the whole image.
4. **Composite deterministically at native size:** retain template at its exact
   native dimensions. Resize only the foreground with premultiplied-alpha filtering;
   place using explicit slot-specific scale/offset and safe-border mask. Layer
   illustration/shadow/frame deliberately, preserve template pixel values outside
   the declared foreground mask. Do not recolor the leather for each new skill.
5. **Review:** side-by-side native32/44/82x93 against originals, plus editable/source
   layers. Same approved subject can have different placement at each size. Check
   contrast, silhouette, tiny details, no clipping/halos and correct state/rank
   reading. User approves motif and visual fit before whole-family expansion.
6. **Integrate:** Frontend owns approved art/layout integration, Build owns config/
   packaging and commits, Content independently checks provenance and real in-game
   composition. No change to source-owned gameplay from art-template adoption.

## Sorcery next step

Keep external master01 and revision02 untouched. Cancel background-color guessing
as the next production step. Ask the existing external image-capable editor for
an isolated original armillary foreground from revision02, with real transparency
and preserved material/core/wisps. The editor is not accessible through this Pi
session; supply the local handoff prompt to the user and await actual file arrival,
not a fabricated dispatch or automatic monitor.

Artist owns isolated technical intake/compositor work under its existing charter,
not shared generator/live templates. Build schedules any bounded heavy processing.
The first use is a private three-size Sorcery composition using selected pinned
blanks; explicitly separate local visual approval from redistribution readiness.

## Regression expectations

Template/output hash manifest; mismatched template size and missing alpha reject;
input master/templates unchanged; output dimensions and frame margins exact;
foreground doesn't overwrite protected border/outside-mask pixels; partially
transparent edges composited correctly; output metadata/provenance explicit;
old32/44/82 templates not silently substituted with resized copies. Tests prove
technical behavior, not image rights or user artistic approval.
