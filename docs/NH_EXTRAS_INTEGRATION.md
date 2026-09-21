# Extras and template-library integration

## User authorization

User confirmed the proposed integration after identifying mismatched quick-save/
quick-load positions and the Pikeman's missing ability art in the native Linux
two-school preview. Prior New Horizons implementation curated ten original bonus
icons and an independent QS/QL overlay; it did not incorporate full VCMI Extras.

Integrate useful compatible Extras components as part of the default player-facing
New Horizons experience, without separate player mod management. Integrate Modder
Tools Pack templates into the authoring workflow, not its executables into the
runtime package. Component compatibility and provenance review remain mandatory.
Existing player packages/profiles must remain intact while a successor is prepared.
This is not authorization to replace unrelated gameplay, activate multiplayer,
ship all mixed-origin resources unreviewed or execute downloaded tools.

## Pinned starting evidence

- vcmi-mods/vcmi-extras branch vcmi-1.7:
  `9229af99b03540df3f247dc7a570a30ef9000670`, root manifest version3.9.4.
- vcmi-mods/modder-tools-pack main:
  `e21c90192cb2faf2c4d41347141248e19283a5ce`.

Extras root and top-level manifests advertise adventure Revisit/Search controls,
Quick Exchange, bonus/ability and spell-immunity icons, extended lobby, portrait
layout, emoji and arrow-tower icons. Chronicles icons have an explicit Chronicles
dependency and keepDisabled flag; do not blindly enable that optional content.
Inventory nested components and actual paths before calling this list exhaustive.
Some items may already have equivalent upstream/runtime functionality; review
consumer mappings and avoid duplicate controls or conflicting implementations.

## First focused player-visible successor

1. Integrate Extras-style adaptive QS/QL toolbar placement, not the detached
   top171/right69 NH24px overlay. Preserve F8/F9 actions and underlying normal
   controls. Upstream compact32px and large64x32 button containers differ; inspect
   parent anchors, breakpoints and portrait mode, not just copy local coordinates.
2. Close creature ability/status icon coverage systematically. Pikeman and
   Halberdier have CHARGE_IMMUNITY in current Castle config, not intrinsic
   FIRST_STRIKE. Extras maps ChargeImmune, FIRSTSTRIKE plus subtype variants and
   E_SHOOT. Our curated ten lack charge immunity/first strike; do not change the
   creature's combat rules to fix a missing image. Audit fallback/tooltip/subtype
   behavior beyond these examples instead of calling a single new icon full Extras.
3. Preserve Sorcery and Light artwork, spellbook redesign, Orders/Doctrines and
   runtime validation. Prepare isolated native Linux and Windows successors through
   Build with exact config/image mappings, identities, source/notices and launch
   instructions. Do not hot-edit the user's running profile.

## Component inventory and admission

For each Extras subcomponent record pinned paths/blobs, purpose, runtime consumers,
current equivalent, dependencies, proposed enabled/conditional/excluded state,
conflicts, licenses/attribution, modifications and validation. Exclusions need an
explicit reason; do not silently curate another unexplained subset. User approval
of integration is not proof that every optional component should be enabled.

Preserve upstream identifiers/dependency semantics where practical and isolate
NH-specific adaptations. Prefer the mod/resource overlay infrastructure over
transliterating every feature into new C++. Resolve known collision with existing
nhConvenience injection so enabling upstream layout cannot create duplicate QS/QL
controls. Compare real effects and callback semantics in this fork before merging
upstream complete widget definitions.

## Templates as a development dependency

The external HoMM3-art workspace already has17 pinned blank templates and an
inventory plus role-specific generation/composition instructions. Expand the
catalog intentionally for needed bonus icons, borders, buttons, portraits and
other roles. Record native dimensions, margins, states, source hashes, provenance
and usage. Templates are authoring inputs selected automatically per request.
Do not distribute the whole tool pack, install it as gameplay content, or execute
bundled Windows tools merely to make templates available. Reuse existing decoding/
composition tools; no duplicate research/toolchain roots.

## Rights and privacy

Do not infer that Extras graphics inherit the engine's GPL. Missing GitHub license
metadata is not a complete per-component license audit: emoji manifest declares
SIL OFL1.1, for example. Inspect actual notices and attribution per selected file.
Do not paste upstream contact email addresses or local workstation paths into a
curated commit unnecessarily. The Tools Pack declares CC BY-SA4.0 but contains
Complete/HotA/game-derived material; declaration is not independent clearance of
underlying pixels. Keep private purchaser references and review-only derivatives
outside public Git/releases. Where an exact asset cannot yet ship, record the
specific gap and use a compatible rights-cleared equivalent without claiming the
upstream asset was imported. No external permission outreach/account changes are
automatically authorized by this plan.

## Ownership and acceptance

Frontend: UI consumer/placement and image adaptation; Build: manifest/config,
component composition, package tooling, sole compile/staging/commits/publication;
Content: independent compatibility, inventory/provenance and sole guarded GUI
checks. Artist's Sorcery charter is not broadened by this request. Runtime only
handles a proved implementation dependency, not speculative new mechanics.

First require exact config/image and duplicate-control regression checks, then
normal-input QS/QL round trip and creature-panel coverage (Pikeman/Archer and
subtype/status samples) on a freshly frozen native Linux candidate. Test supported
compact/expanded layout behavior and retain the user's smoothness report without
inferring exhaustive acceptance. Windows package checks are separate from native
Windows interactive gameplay. No established interactive Windows worker route is
created by this integration instruction.

Keep the first feedback repair small; record remaining Extras component work in
this inventory. A green narrow test, one imported image or source-only preflight
is not complete integration of the authorized scope.
