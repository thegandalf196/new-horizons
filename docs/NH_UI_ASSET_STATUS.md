# New Horizons UI and asset status register

Last audited: 2026-10-05

UP004/UP232 canonical feedback correction in progress: retain the existing
textured leather/red/gold hero-status surface, but replace its historical three
independent action counters with one normal Hero Action Available/Spent state.
Typed additional opportunities belong on ordinary Spell/Order controls with
source and expiry, while generic skill providers retain their own status rows.
Arcane Ballistics ranged hover uses the same attack-local penetration query as
the damage calculation and distinguishes combined PDR from Creature Defense.
No new raster or art approval is implied. These interactions remain Provisional
until native-resolution rendered and playable acceptance; focused source/build
evidence is recorded in the priority queue.

2026-10-05 Perfect Moment source correction verified: canonical automatic first
eligible +5-Luck attack replaces the obsolete manual checkbox and local armed
state. Reuse the existing Hero Action panel and footer; remove the declaration
control rather than inventing new art or a substitute button. Existing perk
art approval status is unchanged. Source/native/build evidence belongs to UP023;
client/native builds,19 focused native cases and11 client source guards pass.
Rendered acceptance and Linux playable delivery remain separate and unclaimed.

2026-10-05 Mandate of Heaven integration: the generic Divine Mandate resource
row already uses the shared dynamic cap. Completed-pair history must remain
visible if a later rank/perk change lowers that cap; only remaining uses clamp
to zero. No layout, background, icon or artwork changes are required. Its
NH_perk_neutral fallback remains Not done art; focused acceptance is in UP108.

2026-10-05 Knightly Sequence integration: existing Order numerical previews
and their cache key consume the combined captured Divine Mandate efficiency,
with Sacred and Knightly provenance kept separate. No layout/material/art change
or graphical acceptance is claimed. Its generic NH_perk_neutral remains
Not done artwork; implementation and focused acceptance are tracked in UP108.

2026-10-05 Sacred Command integration: the existing Order chooser's numerical
preview now receives the distinct prepared Divine Mandate efficiency snapshot,
including Iron Discipline and Second Wind calculations. Native layout, frames,
materials and artwork are unchanged. No visual approval, new art, or Final
classification is implied; focused runtime validation remains under UP108.

This is the maintained UI/art status index for the current New Horizons working tree. It records live bindings and remaining visual/UI work. It does not replace gameplay specs, source configs, runtime manifests, or acceptance records. The row-level index is [NH_UI_ASSET_INVENTORY.csv](NH_UI_ASSET_INVENTORY.csv).

The audit used canonical registries and current source bindings rather than counting files. The earlier combat-spell binding audit recorded 104 entries, 100 enabled; Animate Dead, Clone, Weakness and Counterspell were inactive rows. These include noncanonical compatibility identities; current canonical identity coverage is separately tracked as 61/67 in the functional matrix. The five Neutral Adventure Spells are tracked separately. It also covers all 31 registered secondary skills, all 310 perk definitions, all eight Order bindings, and the scoped hero, spellbook, convenience, creature-info, and ranked-recruitment UI surfaces below. Active means enabled in source configuration, not necessarily promoted to the playable build. Unregistered Magic Missile is noted separately as non-live; Spell Lock is registered and has an active-profile native consumer test. The row-level CSV's spell bindings still need a fresh full reconciliation before being used as current totals; its perk activation labels are checked against the current registry.

This is a binding inventory, not a full visual audit or a product completion claim. No game was launched and no GUI review was performed for this register. A resource path, generated manifest, native-size file, or implemented code path does not by itself establish final art or accepted UI. `tools.tests.test_new_horizons_ui_perk_inventory` now checks all 310 perk rows against the current activation registry and ensures active neutral fallbacks remain classified as Not done art.

## Status meanings

UP230 user-priority correction: all26 custom live spell raster families were
audited, not just the screenshot's Vengeful Vines. Their78 original30/32/44px
runtime files are opaque RGB paintings. The native parchment wrapper is valid
but cannot remove painted pixels. The new spell-symbol policy is persistent in
NH_HOMM3_UI_STYLE_GUIDE.md. Transparent symbol revisions are being integrated
through the built-in image generator and HoMM3 art skill. All26 new masters
and native32/44 comparison sheets are inspected; original masters stay intact.
All78 runtime PNGs are integrated; two final alpha/hash/native-dimension/binding
tests pass, plus eight content-binding checks. Converted identities and hashes are
tracked in assets/new-horizons/art-source/spell-symbols-v2/manifest.json.
Do not call the whole icon request complete until every listed family is
converted and inspected. New art remains **Provisional**, not user-approved Final.

The standalone Adventure Spells access button/label is removed in source.
The existing selected faction/tier exterior picture supplies the hotspot and
gold rectangular hover border, preserving the native purchase panel and saved
rules gating. Client92271 builds; the focused source guard passes. All five
authored Roman-numeral guild names now use Mage Guild Level4/5 consistently;
the nine-faction data guard passes9/9. Gold hover appearance, input teardown
status cleanup and in-game visual acceptance remain unverified. No GUI launch
or playable snapshot promotion occurred.

UP227 Vault of Ashes: dedicated hall/town artwork **Not done**. Its functional
Horde2 hall card references purchaser HALLELEM.DEF frame33 (the related Fire
dwelling) as a temporary binding, without extracted pixels, generated artwork,
new frame composition or a bespoke town-scene structure. The native fourth
unique-building card reuses existing hall layout. This is not approved Vault
art or rendered acceptance; future bespoke art must use the HoMM3 art skill.

UP212 Mage Guild scroll binding: **Provisional** source integration for standalone
NH iconScroll32/44 images. The client now references native TPMAGES.DEF group0
frame0 from purchaser H3sprite.lod: actual blank open83x61 parchment. Original
SPELLSCR composites remain untouched; small emblems retain native size, oversized
aliases aspect-fit inside54x45, and the full parchment remains the hitbox.
No extracted pixels, uncertain Modder Tools Pack template, new art or dependency
is shipped. The blank reference resolves the container binding, not every icon's
visual construction: UP230 now supplies transparent emblems for all26 custom
raster families, with final alpha/hash gates passing. The earlier opaque
paintings are retained as historical sources, not accepted scroll artwork.
Client86675 builds and independent Astra
review finds no blocking source issue; existing Mage Guild asset guards pass5/5.
No GUI run,
native-resolution composed-widget acceptance or playable promotion yet.

UP211 Conflux independent Core recruitment: implementation in progress.
Pixie and Sprite are separate recruitment choices, using the supported eighth
dwelling and original artwork referenced externally. Existing Sprite artwork is
not a newly authored or approved independent-town asset. Hall/Fort layout and
co-located town structure selection require native-resolution visual review;
classify the new binding as **Provisional**, not Final. No GUI run, extracted
original art, newly generated art or playable acceptance is claimed.

UP203 indiscriminate-spell friendly-fire confirmation: **Provisional** interaction,
source/native verified2026-10-04. Reuse the existing native framed yes/no dialog and
ordinary text/stack names, rather than creating decorative assets. Required
content is the actual potentially affected friendly-stack preview. Source,
native guard/preview tests and rendered interaction acceptance are distinct;
client16088 and both-target64537 build, and native71900 passes6/6, including
recipient preview and confirmation gate. These are not rendered callback or
dialog acceptance; the existing native frame alone does not prove visual quality.
No new artwork or GUI execution is authorized by this entry.

UP190 Puppet Master interaction is source/native verified (8/8 principal
cases, successful client/test builds), not visually accepted.
Its single-target spell interaction reuses existing native controls; controller
dispatch changes are behavior work, not a new panel design. Puppet Master and
the internal Lucidity status currently reference Hypnotize icons/animation;
both purpose-made icons are **Not done**, not provisional completed artwork.
No new art has been created and no graphical or playable acceptance is claimed.

UP181/182 source/native checkpoint: Mountaineer and Grand Tactics are active
after successful client/test builds, principal12/12, adjacent6/6 and active
registry12/12 checks. Existing perk/deployment controls are reused; generic UI
remains Provisional and both neutral fallback icons remain Not done art.
Rendered deployment handoff and full AI deployment still need Phase2 acceptance;
no local playable snapshot or graphical review is claimed.

Battle Plan interaction (UP-148): implemented with source/native evidence. The existing
painted Orders panel names the free opening choice, disables decline/Perfect
Moment while pending, and returns to the chooser after target cancellation.
Four focused source guards pass; the interaction is Provisional pending rendered
acceptance. The perk is active after both-target builds and principal5/5 native
acceptance; production-registry final checks pass30/30. Its neutral fallback
is Not done art, and no new artwork or playable promotion is claimed.

Double Command interaction (UP-147): implemented with source/native evidence.
The ordinary
Orders panel supplies the mandatory immediate choice and names its source;
target cancellation returns to the chooser without spending the opportunity.
Existing painted panel/buttons are reused, with no new artwork. The interaction
is Provisional pending rendered review. Double Command is active after both-target
builds, principal 6/6 and activated focused 47/47 native acceptance. The four
focused client source guards pass; they do not prove graphical behavior. The
neutral fallback remains Not done artwork, and no playable promotion is inferred.

Bastion source/native checkpoint (UP-135): named first-hit combat feedback and
shared Defend/Hold the Line eligibility have focused live/detached8/8 evidence.
Registry173/310 active,137 planned. Generic UI Provisional; purpose-made art
Not done. No rendered or playable acceptance is claimed.

Mine Layer source/native checkpoint (UP-137): shared count and server validation
support the additional mine, including five-mine AI placement. Principal10/10
passes; active registry coverage is172/310,138 planned. Generic perk UI is
Provisional, purpose-made art Not done; no rendered/playable acceptance claimed.

Unbreakable source/native checkpoint (UP-131): named combat feedback identifies
the first negative-Morale suppression each round, separate from Rally. Native
10/10 and data/inventory35/35 pass with production registration enabled. Registry
coverage is171/310 active,139 planned. Generic UI is Provisional; the neutral
fallback is Not done artwork. No rendered or playable acceptance is claimed.

Broad Muster source/native checkpoint (UP-128): the existing native scrollable
Muster picker retains solo choices and adds exact positive allocations naming
both Core dwelling destinations. Localization and static wiring pass; both
Linux targets build and activated native18/18 passes, zero skips. Registry
coverage is170/310 active,140 planned. Purpose-made perk art is Not done (neutral
fallback), generic UI is Provisional. Long labels need native-resolution review;
no GUI acceptance or playable promotion is inferred from these checks.

External Recruiter source/native checkpoint (UP-124): native recruitment controls
reuse the existing Muster button and dialog for owned external Core dwellings.
Fixed+2, weekly status and shared free-tier-one costs are wired; both targets
build and activated focused native11/11 passes with zero skips. Registry coverage
is169/310 active,141 planned. The neutral perk icon is Not done art, generic UI
is Provisional, and no rendered/playable acceptance or promotion is claimed.

Blink source/native checkpoint: a purpose-made Provisional knight/echo icon
has retained master, exact prompt, provenance and 44/32/30-pixel exports under
`assets/new-horizons/art-source/blink-v1/`. Source registration and the shared
legal-destination hover wiring are present. Both Linux targets build and all
12 focused runtime/rules/AI cases plus 24 immunity/Entangle guards pass,
zero skips. No final-art claim is made. Rendered hover legibility and actual teleport animation/sound remain
separate acceptance work.

Source delta, not playable acceptance: Blink brings the current
settings to 99 entries/96 enabled, and Blinkmaster to 115 active/195 planned
perks. Rendered/playable checks and Blinkmaster's bespoke perk art remain open.

Every row has separate Implementation and Art columns. Each column uses only these three values:

- Not done — the live UI behavior/binding is absent, or the artwork is missing or a mismatched stand-in.
- Provisional — a source binding or usable art exists, but visual approval or the remaining integration/layout check is not evidenced.
- Final — this item has explicit user art approval, or uses an exact, suitable classic Heroes III art binding under the user-authorized reuse policy in [NEW_HORIZONS_MVP.md](NEW_HORIZONS_MVP.md). Final art does not mean the UI integration has passed graphical acceptance.

Implementation is Final only when the intended UI behavior and its presentation have been accepted. The CSV's Remaining field explains each classification and points to current sources.

## Current status summary

UP-234 implementation checkpoint: custom spellbook frames now center inside
their school-border canvas, with original DEF placement restored between page
changes. Temporal Field and Selective Dispel use viewport-centered choices.
The Vengeful Vines three-hex selector and shared House of Wisdom parchment
presentation are implemented. Existing artwork remains unchanged and
Provisional; these source corrections are not rendered acceptance or a new
playable promotion. Full scroll hitboxes, native scale and vanilla controls
must be checked in the finished client before closing the reported defects.

UP-140 Steadfast: generic legal offer/name/help presentation is Provisional;
purpose-made art remains Not done with NH_perk_neutral fallback. Source/native
principal14/14 passes, registration174/310 active and136 planned. No new art,
graphical acceptance or immutable playable snapshot is claimed.

UP-120 Spellward source/native checkpoint: generic offer/name/help presentation
is active with22/22 focused damage/ownership checks. `NH_perk_neutral` remains
the fallback, so bespoke art is Not done and UI presentation Provisional.
Current perk coverage is168/310 active,142 planned. No new artwork was generated,
no GUI was launched and no playable snapshot was promoted.

UP-118 source/native checkpoint: Earthquake and Geomancer have authoritative
field/siege, targeting, feedback and AI evidence. Fractured Ground borrows
Quicksand's external animation reference, not newly authored terrain art;
purpose-made art is Not done. Geomancer still uses the neutral perk fallback
(Not done art). Generic presentation remains Provisional until rendered review.
Current perk coverage is167/310 active,143 planned. No GUI or playable promotion.

UP-115/116 source/native checkpoint: Sanctuary Keeper and Venomancer are
active with generic perk offer/name/help presentation. Their exact lifetime
and Poison snapshot effects, detached forecasts and actual AI casts have
focused native evidence. Both still bind `NH_perk_neutral`: bespoke art is
Not done, and UI implementation is Provisional until rendered/playable review.
Coverage is166/310 active perks,144 planned. No new artwork was generated,
no GUI was launched and no playable snapshot was promoted in this cycle.

UP-114 first-slice source binding: distinct Mass Curse and Mass Sorrow entries
reuse their own base spell's exact classic icons, sound and affected-stack
animation by reference; no extracted art is redistributed. The distinct names
and normal spell selection distinguish the variants. Both source rows and
Grand Malediction are active with48/48 focused native checks. These bindings are Provisional
until in-game legibility and selection are reviewed, not new commissioned or
approved Mass artwork. This first-slice checkpoint is historical: Mass Bless
and Litany are now active with55/55 focused native checks, including capped and
uncapped School duration, Benediction, prior-family refresh, Curse removal,
both immunities and paid-cost/forecast parity. Bless resources are likewise
referenced, not extracted, and remain Provisional pending in-game review.
Litany's generic neutral fallback is Not done art. Mass Regeneration now reuses
the purpose-made base Regeneration icon and its existing sound/animation refs;
this is Provisional variant art, not a newly authored Mass icon. Communion's
generic perk fallback remains Not done art. Distinct Mass Slow now uses classic
Slow icons, animation and sound by reference, with its distinct selectable name.
Temporal Field's purpose-made Provisional icon remains bound. The old toggle
dialog is suppressed only for profiles with the distinct entry; historical
profiles retain it. Native grant/cast/AI evidence passes; actual selection and
legibility still need in-game review. No GUI or playable promotion is claimed.
Current settings contain106 entries,102 enabled; this supersedes the104/100
source count in the earlier audit. Mass Slow art is Provisional, not new authored
or approved variant art; no extracted original resources are redistributed.

UP-064 main-menu branding: eight bounded HoMM3-skill reference-based subtitle
patches cover the available Complete/Armageddon's Blade main, scenario-selection
and loading illustrations. Native 800×600 private compositions were inspected;
their generated gold lettering remains Provisional, not user-final art. Full
reference backgrounds are not shipped. Runtime bindings select by the exact
installed payload CRC and fall back visibly for unsupported variants. The user
accepted using these available references; a distinct Shadow of Death title is
not included. A passive native small-yellow product-version label uses the same
authoring version as the live module. Client compilation and nine focused checks
pass; actual in-game rendering remains pending. Neither source bindings nor
composited previews are playable
acceptance. See UP-064 and `assets/new-horizons/art-source/menu-titles-v1/`.

Grand Metamagic no longer has an activation button in the battle strip or
spellbook. Its automatic continuation uses the existing Spell Action count;
no new artwork or permanent skill-resource slot is introduced. This control
removal does not change the provisional approval status of the perk icon.

| Component | Implementation | Art | Evidence and remaining work |
| --- | --- | --- | --- |
| Hydra's Vitality spell icon and capacity feedback | Provisional | Provisional | Original three-headed hydra painting has 44/32/30 exports, retained master, exact prompt, provenance and inspected native/enlarged comparison under `assets/new-horizons/art-source/hydras-vitality-v1/`. Book/scroll/scenario/effect/immune bindings are registered. Both Linux targets link; eight runtime/AI cases, sixteen existing health guards and the UI source guard pass. Capacity preview and next-activation status use shared values. Rendered/playable acceptance and final art approval remain pending. |
| Verdant Prison spell icon and targeting | Provisional | Provisional | Original root-cage painting has 44/32/30 exports, master, exact prompt, provenance and native/enlarged comparison under `assets/new-horizons/art-source/verdant-prison-v1/`. Book/scroll/scenario/effect/immune bindings are registered; summon assets are referenced without copying purchaser pixels. Both Linux targets link; eleven runtime/AI cases, ten Trolls guards and targeting source checks pass. Rendered/playable review and final art approval remain pending; Verdant Warden perk art remains Not done. |
| Summon Trolls spell icon and targeting | Provisional | Provisional | Original moss-stone Troll idol has 44/32/30 exports and retained master, exact prompt, provenance and native/enlarged comparison under `assets/new-horizons/art-source/summon-trolls-v1/`. Book/scroll/scenario/effect/immune bindings are registered; C18SPW0 and SUMNELM are referenced without copying purchaser pixels. Both Linux targets link; ten focused runtime/AI cases, two existing AI guards and the targeting source guard pass. Rendered/playable review and final art approval are pending; Beastcaller perk art remains Not done. |
| Vengeful Vines spell icon and targeting | Provisional | Provisional | Purpose-made HoMM3-art winding thorn vine has 44/32/30 RGB exports and retained master, exact prompt, provenance and native/enlarged comparison under `assets/new-horizons/art-source/vengeful-vines-v1/`. Book/scroll/scenario/effect/immune bindings are registered; SP02_ and BIND are referenced without copied purchaser pixels. UP-234 replaces historical six-way rotation/confirmation with three connected selected hexes, third-click submission, Backspace undo and battlefield preview. Both Linux targets link and the UI source guard passes. Rendered/playable and final approval remain pending. |
| Entangle spell icon and root status | Provisional | Provisional | Purpose-made HoMM3-art boot-and-roots 44/32/30 RGB exports are bound to book/scroll/scenario/effect/immune roles. Master, exact prompt, provenance and native-size comparison are retained under `assets/new-horizons/art-source/entangle-v1/`. Existing SP02_ animation and BIND sound are referenced without copying purchaser pixels. The stack status reads actual remaining root rounds and distinguishes movement restraint from Time Stop. Both Linux targets link, and 17/17 focused native cases plus 4/4 status wiring checks pass. In-game rendering and user-final approval remain open. Rootcaller's neutral perk fallback is Not done art. |
| Army split dialog | Provisional | Provisional | User confirmed garrison-to-garrison and hero-to-garrison transfers work, but rejected the earlier dialog's pasted full-width gold seams and cutout-like composition. The latest source uses a continuous leather field, the classic outer frame, and individual beveled creature, owner, slider, amount, and button wells based on the user's [visual reference](https://i.imgur.com/8nKTXds.jpeg). Source guards and Linux client build pass. A private in-game `Split Imps` capture from unpromoted candidate `344129b2…` shows the seams gone and the controls visible at game scale; texture join/button-well aesthetics and garrison/hero variants still need user review. The earlier seam-removal snapshot remains promoted; this latest well-refinement has **not** been promoted. Existing portrait/crest/button art is reused, not newly generated. |
| Skill-provided combat resources | Provisional | Provisional | Optional skill metadata declares a typed read-only status provider and localized help. Generic entries reuse the learned rank's existing icon and show current/maximum values separately from round Actions. Metamagic and Bloodrage are the first two typed Skill providers; Counterspell and Warcasting use the same row renderer for non-Skill state. Actual visible height controls panel placement, without a reserved Metamagic slot. Source review, metadata tests and focused UI guards pass; native client CI and graphical acceptance remain pending, including compact stack overlays and sticky panels on/off. |
| Magic Arrow Overcharge outcome comparison | Provisional | Provisional | The selected-target dialog compares damage and estimated casualties at zero and selected Overcharge, refreshing on amount changes through the shared effect forecast. Resistance chance is explicit; unavailable forecasts do not block legal casts. The compact 320×250 layout reuses the game's leather texture and standard bordered-window frame, replacing the flat 420×340 panel. The client rebuild and source checks pass; rendered layout and interaction acceptance remain pending. |
| Learned-skill perk browser | Provisional | Provisional | Left-click opens saved-registry perks grouped by Basic, Advanced and Expert, with names, existing icons, learned state and implementation status. Right-click uses shared perk help and the parent-skill component. Native client build and focused source guard pass; rendered layout, long names and pointer interaction remain unverified. Reuses the existing leather dialog and perk art; fallback icons remain Not done in their individual inventory entries. |
| Six custom-school bookmark pairs | Provisional | Final | The user has explicitly classified the six schools' selected/unselected bookmark art as Final. All six descriptor bindings exist in [mod.json](../Mods/new-horizons/mod.json). [CSpellWindow.cpp](../client/windows/CSpellWindow.cpp) uses frames 0/1 and aspect-preserving 68×51 compact rendering for six schools in the small book. Runtime layout and state behavior still lack a recorded GUI acceptance pass. |
| Six school headers and spell-mastery borders | Provisional | Provisional | All six headers and four-frame border descriptors are bound in mod.json. The header is page-one-only and borders follow school proficiency 0–3. User approval of these assets is not recorded. Review header transparency, all four corners/levels, spell overlap, and book layouts. |
| School emblems and school button states | Not done | Provisional | Image files/descriptors exist under Mods/new-horizons/Images, but SpellSchoolHandler currently reads only schoolBorders, schoolBookmark, and schoolHeader. No live consumer for the additional emblem/button family was found in the current school config/code. Keep these outputs provisional and do not count them as integrated school UI. |
| NH-authored secondary-skill rank families (17) | Provisional | Provisional | All 17 families have live image slots. Metamagic's rank family is explicitly Provisional. The custom families have no recorded final visual approval; verify the full rank/size grid in product UI. |
| Exact classic secondary-skill art (10) | Provisional | Final | Ten retained skill identities use matching original rank art under the authorized original-art reuse policy. Runtime layout remains unreviewed. |
| Reworked/renamed classic rank bindings (4) | Provisional | Provisional | War Machines, Discipline, Command and Spellcraft borrow original frames, but semantic fit and user visual approval are not established; review or replace the bindings. |
| Existing core spell icons | Provisional | Final | The 63 live core combat-school spell IDs retain their matching classic spell identity/art; the five Neutral Adventure Spells are inventoried separately below. The user-authorized art policy allows suitable exact original art to be referenced in place. The in-game presentation remains unreviewed. |
| Summon Boat adjacent destination targeting | Provisional | Provisional (reused native overlay) | UP056 source/native verified: shared adjacent-water legality, explicit selected destination and native spell-range mask contrast. Invalid tiles are shaded while legal destinations remain bright, with the existing native sailing cursor. Both build targets, 8/8 focused native cases and the narrow UI source-contract guard pass. No new artwork, panel or decorative marker. Native-resolution contrast/cursor and actual selection/cancellation still need rendered/playable review; do not mistake existing spell-icon approval for targeting acceptance. |
| Dimension Door range mask and ends-Movement warning | Provisional | Provisional (reused native UI) | UP056 shared visible/legal radius8 predicate drives existing map contrast. A localized NH-only warning uses the native status bar on targeting entry/hover, with matching-text cleanup on exit/cancel; no new artwork or panel. Both targets build and 13/13 focused native cases plus manual wiring/translation guards pass. Native-resolution contrast/text fit and actual input/cancellation remain unverified; protected-barrier enforcement still missing. Not graphical or playable approval. |
| Five Neutral Adventure spell icons | Provisional | Final | Summon Boat, Water Walk, Town Portal, Fly, and Dimension Door are registered at fixed Mage Guild levels and retain their exact original spell graphics by identity. Verify the guild/book and once-per-day presentation in GUI. |
| Six New Horizons spells using Magic Arrow frame 15 | Provisional | Not done | Counterspell, Disintegrate, Focus Magic, Phantom Army, Spell Lock and Time Stop use the Magic Arrow icon frame for book/scroll/bonus art. Focus Magic is live with provisional status feedback; Time Stop's live binding is verified independently below. Purpose-made spell art remains missing. |
| Two New Horizons spells reusing related core frames | Provisional | Provisional | Master Chain Lightning reuses Chain Lightning frame 19 and Transfigure Matter reuses Remove Obstacle frame 64. Both await visual approval. |
| Sanctuary spell icon | Not done | Provisional | Purpose-made original Level-1 Light art is bound to book, scroll, status/immune, and scenario icon roles as 44×44, 32×32, and 30×30 RGB PNGs under `Mods/new-horizons/Images/NH_spell_sanctuary_{44,32,30}.png`. The retained master, exact prompt, export manifest, comparison, and copy hashes are under `assets/new-horizons/art-source/sanctuary-v1/`. Native-size legibility and dimensions were checked; no in-game rendering or user-final approval is recorded. |
| Holy Wrath spell icon | Provisional | Provisional | Purpose-made 44×44 book/scroll and 30×30 effect/immune icons are bound from the HoMM3 art workflow. The 32×32 alternate and generation source are retained. The unrelated `SPELLBON.def:0:15` scenario-bonus frame is still Not done, and native graphical review/user-final approval are pending. |
| Holy Armor spell icon | Provisional | Provisional | Purpose-made 44×44 book/scroll, 32×32 scenario and 30×30 effect/immune exports are bound to the Level-2 Light spell. The retained master, exact prompt and comparison are under `assets/new-horizons/art-source/holy-armor-v1/`. Focused authoritative, damage and AI tests pass; native rendering, lifecycle/save interaction, and user-final approval remain open. |
| Crusade! spell and active status | Provisional | Provisional | Uses appropriate original Prayer icon/animation/sound assets by reference, with no purchaser pixels copied into the repository. Active status reports actual Attack/Defense, flat Initiative, fractional magical reduction and rounds. Four native status checks and two wiring checks pass within the 19-case Crusade checkpoint. This is related reference reuse, not newly authored or approved final art. In-game rendering and user-final approval remain open. Crusader's perk icon remains a neutral fallback and is Not done. |
| Guardian Spirit spell icon | Provisional | Provisional | Original 44×44, 32×32 and 30×30 exports and provenance are retained under `assets/new-horizons/art-source/guardian-spirit-v1/` and bound to the spell's book/scroll/scenario/effect roles. Native rendering and user-final art approval remain open. |
| Heavenly Gale spell icon and active status | Provisional | Provisional | Original 44×44, 32×32 and 30×30 shield-and-gale exports from the HoMM3 art workflow are bound to the Level-3 Light spell. The retained master, exact prompts, provenance and native-size comparison are under `assets/new-horizons/art-source/heavenly-gale-v1/`. The existing battle spell-status row now reads the saved reduction percentage and remaining rounds. Source binding/content checks pass; native rendering, full mechanic/AI checks and user-final approval remain open. |
| Divine Retribution spell icon and Judged status | Provisional | Provisional | Original 44×44, 32×32 and 30×30 justice-scale exports from the HoMM3 art workflow are bound to the Level-4 Light spell. The retained master, exact prompt, provenance and native-size comparison are under `assets/new-horizons/art-source/divine-retribution-v1/`. Battle status integration, native mechanic/AI evidence, in-game rendering and user-final approval remain open. |
| Storm of Daggers spell and impact art | Provisional | Provisional | Original 44×44 book/scroll, 32×32 scenario-bonus, and 30×30 effect/immune icons are source-bound. The four-frame dagger impact is registered as an `affect` animation for each target; an optional cast cue exists but is not bound. The `MAGICBLT` cast sound remains provisional. Art provenance and export checks are retained under `assets/new-horizons/art-source/storm-of-daggers-v1/`. Native rendering, timing, and user-final approval remain open. |
| Shield of Chaos spell art | Not done | Not done | UP-063 uses original Shield frame references and C13SPE0/SHIELD feedback to exercise the spell. These are not purpose-made art; bespoke Chaos art must use the HoMM3 art workflow. No rendered or playable approval is claimed. |
| Hand of Fate spell art | Not done | Not done | The base spell is being implemented for UP-057. Its book/scroll/status/immune/scenario icons borrow unrelated Magic Arrow frames, with C18SPW0/MAGICBLT impact/sound references. These bindings are exercise hooks, not authored provisional art or rendered/playable acceptance. Purpose-made art must follow the HoMM3 art skill. |
| Regeneration spell icon | Provisional | Provisional | Original 44×44 book/scroll and 30×30 effect/immune icons are source-bound; the 32×32 export is retained. Provenance and native-size export checks are under `assets/new-horizons/art-source/regeneration-v1/`. Focused authoritative/AI tests and the client link pass; native rendering and user-final art approval remain open. |
| Nature Poison spell icon | Provisional | Provisional | Original 44×44 book, 32×32 scroll/scenario, and 30×30 effect/immune icons are source-bound to the new `new-horizons:poison` hero spell. The purpose-made master, prompt, and export comparison are under `assets/new-horizons/art-source/nature-poison-v1/`. This binding does not replace the older `core:poison` creature-ability icon. Native rendering and user-final art approval are open. |
| Life Drain spell icon | Provisional | Provisional | Purpose-made 44×44 book, 32×32 scroll/scenario and 30×30 effect/immune icons depict vitality flowing from a dark hand to a gold hand. The HoMM3-art master, exact prompt, manifest and native-size comparison are under `assets/new-horizons/art-source/life-drain-v1/`; the new Shadow spell definition references the exported runtime files. In-game rendering and user-final art approval remain open. |
| Soul Chain spell icon and target/status UI | Provisional | Provisional | Purpose-made 44×44 book/scroll, 32×32 scenario and 30×30 effect/immune icons are bound to the Level-3 Shadow spell. Its HoMM3-art master, prompt, provenance, manifests and native-size comparisons are retained under `assets/new-horizons/art-source/soul-chain-v1/`. The ordered primary/secondary selector and link-status readback have source review and focused mechanical evidence, but native-resolution rendering, long translated names, input flow and user-final art approval remain open. |
| Shadow Gift spell icon and sacrifice/status UI | Provisional | Provisional | Purpose-made 44×44 book/scroll, 32×32 scenario and 30×30 effect/immune icons are bound to the Level-3 Shadow spell. The selected HoMM3-art master, prompt, provenance, manifests and native-size comparison are retained under `assets/new-horizons/art-source/shadow-gift-v2/`; the clipped v1 draft is marked superseded. A compact leather 10/20/30% choice dialog and separate timed-enchantment/battle-long-cap stack indicators compile in both Linux targets; the active-profile focused mechanic/AI filter passes 8/8. Native-resolution rendering, live interaction review, playable delivery, and user-final art approval remain pending. |
| Vampirism spell icon and stack status | Provisional | Provisional | A purpose-made 44×44 book/scroll, 32×32 scenario, and 30×30 effect/immune icon set is bound to the Level-4 Shadow spell. Its source master, prompt, provenance, export manifest, and native-size comparison are retained under `assets/new-horizons/art-source/vampirism-v1/`. The battle stack status reports current lifesteal and remaining rounds. Both Linux targets link and 15 active-profile runtime/AI cases plus three UI source checks pass. Native-resolution rendering, playable delivery, and user-final art approval remain open. |
| Re-animate spell icon and temporary-creature status | Provisional | Provisional | Purpose-made 44×44 book, 32×32 scroll/scenario, and 30×30 effect/status icons are bound to the Level-4 Shadow spell. Its master, exact prompt, provenance, manifests, and native-size comparisons are retained under `assets/new-horizons/art-source/reanimate-v1/`. The stack panel reads the existing one-battle restoration ledger and shows a generic Temporary count. Both Linux targets link and a focused UI source guard passes; native rendering, playable delivery, and user-final art approval remain open. The reused Animate Dead sound/impact remains provisional. |
| Soul Reaper spell icon | Provisional | Provisional | Purpose-made 44×44 book, 32×32 scroll/scenario, and 30×30 effect/immune icons are bound to the Level-5 Shadow spell; master, prompt, provenance, and exports are retained under `assets/new-horizons/art-source/soul-reaper-v1/`. The 32×32 scenario-bonus binding still needs actual consumer-size reconciliation. Native rendering and user-final approval remain open. |
| Doom spell icon and stack status | Provisional | Provisional | Purpose-made chained-mask 44×44 book, 32×32 scroll/scenario, and 30×30 effect/immune icons are bound to the Level-5 Shadow spell. Master, prompt, provenance, manifest, and native-size comparison are retained under `assets/new-horizons/art-source/doom-v1/`. The stack-status source guard passes 3/3 and the Linux client links; native in-game rendering, interaction, and user-final approval remain open. |
| Non-neutral active perk icon bindings | Provisional | Provisional | The active registry has 126 perks; named bindings in `NewHorizonsPerkIcons.h` include the purpose-made Empower Spell, Malediction, Soul Binder, Night Feeder, and Reanimator paintings. Binding alone does not prove final art or in-game approval. The focused art checks validate individual masters and runtime states; neutral-fallback gaps remain classified as Not done art. |
| Active perk icon gaps | Provisional | Not done | Archery perks, active Bulwark perks, Light Magic's Benediction, and Nature Magic's Herbalist still include neutral-fallback bindings. These mechanics are active in source, but their perk icons are not all purpose-made; follow the HoMM3 art skill for every provisional replacement. Do not mistake the physical-Poison status icon reuse for a Toxic Spines perk icon. Recount this row after each activation and binding pass. |
| Shadow Magic Malediction perk icon | Provisional | Provisional | A purpose-made thorn-bound hourglass master was generated with the HoMM3 art workflow; its prompt, 44×44/32×32 comparisons, four 44×44 runtime states, and descriptor are retained under `assets/new-horizons/art-source/active-perks-v5/` and `Mods/new-horizons/Images/`. The named client binding is present. At 44×44 the hourglass remains legible; native in-game rendering and user approval are pending. |
| Shadow Magic Soul Binder perk icon | Provisional | Provisional | A purpose-made gauntlet-and-three-rings painting has four 44×44 runtime states, an explicit perk binding, and retained master/prompt/provenance/export manifests under `assets/new-horizons/art-source/soul-binder-v1/`. Native-size inspection passed, but in-game rendering and user-final art approval remain open. |
| Shadow Magic Night Feeder perk icon | Provisional | Provisional | A purpose-made 44×44 medallion painting has four runtime states, a named perk binding, and retained master/prompt/provenance/export manifests under `assets/new-horizons/art-source/night-feeder-v1/`. Native-size inspection passed; in-game rendering and user-final approval remain open. |
| Shadow Magic Reanimator perk icon | Provisional | Provisional | A purpose-made three-helm painting has four native 44×44 runtime states, a named perk binding, and retained master/prompt/provenance/export manifests under `assets/new-horizons/art-source/reanimator-v1/`. Native-size comparison passed; in-game rendering and user-final approval remain open. |
| Physical Poison battle status | Provisional | Provisional | The stack panel reads public physical-Poison state, displays remaining activations and next tick, and updates without moving the cursor when the state changes. The Linux client and native focused UI test pass; independent source review found no blocker. It reuses the classic Poison SpellInt frame as a temporary status icon, not as Toxic Spines perk art. Native-resolution inspection, especially overflow dots, remains pending. |
| Planned perk definitions (181) | Not done | Not done | Planned entries are not active gameplay. The current registry has 129 active and 181 planned perks; the row-level CSV's perk activation labels are checked against the registry after each activation. The neutral fallback is not artwork. |
| Eight Order icons and action controls | Provisional | Provisional | Eight action-window descriptors and order icons are present and referenced by BattleHeroActionWindow.cpp. No user-final art approval or recorded in-game review is present. |
| Hero attributes, capabilities, growth/mastery and category icons | Provisional | Provisional | NH images are present and bound from hero/growth/town UI source. Their actual rendered layouts have not been accepted in a GUI pass. Leadership, Siege and Movement are called out in the CSV. |
| Split/transfer dialog owner indicators | Provisional | Final | [GUIClasses.cpp](../client/windows/GUIClasses.cpp) places built-in `PortraitsLarge` hero portraits and `CREST58` player crests below both creature panels; bounded name fallbacks cover missing or ambiguous markers, with full text available on hover/right-click. The generated 298×440 frame in [AssetGenerator.cpp](../client/render/AssetGenerator.cpp) preserves the installed `GPUCRDIV` top, side rails and ornate lower edge while extending the leather panel; it is player-colored and expands/recenters the window. The tallest fallback-layout button ends at y391, with the lower frame beginning at y403. Source guard checks containment and fallback bounds; actual game-scale rendering and interaction remain unverified. No extracted artwork is included. |
| Buffer spell-points UI | Provisional | Not done | [SpellPointPresentation.h](../client/windows/SpellPointPresentation.h) supplies total/maximum, Buffer annotation and explanatory tooltips. The earlier shared yellow-markup fix was promoted. UP-002 corrects the separate adventure/town compact cards: two bounded lines, yellow Buffer, exact right-click help and actual capacity for accessible heroes only. Enemy Visions retains hidden capacity. Client build and seven focused native tests pass. This follow-up is not promoted; rendered legibility checks remain pending. The battle sidebar is a separate consumer. Sparkle artwork is not bound; a preview mockup is not a runtime asset. |
| Fortress/Conflux ranked recruitment presentation | Provisional | Provisional | [CCastleInterface.cpp](../client/windows/CCastleInterface.cpp) contains ranked recruitment cards and a contained-portrait path for compact Conflux cards. That is source evidence only; no GUI acceptance is recorded. The 14-row Conflux category mapping remains a proposal and Firebird's Champion category is explicitly provisional in [NH_CONTENT_ACCEPTANCE.md](NH_CONTENT_ACCEPTANCE.md). The town-card composition and roster/category presentation still need review. |
| Tower construction hall relocated cards | Provisional | Provisional | Replaces the vanilla bitmap's baked card area with referenced leather and per-card frames for the New Horizons Tower hall. This addresses the mismatch between relocated Library/dwelling cards and old background slots. Title/footer and other towns remain unchanged. Source guards and independent review pass; the newer Windows build and graphical verification remain pending. |
| Extra Mage Guild levels for Castle, Stronghold and Fortress | Provisional | Provisional | Owner-supplied source packages are retained under `assets/new-horizons/Mage Guilds/`; Castle V and Stronghold I–V remain unchanged. UP213 replaces all five Fortress levels with the supplied v9 native DEF animations, individual masks/campaign icons and hall cards. Static levels I–II and 21-frame levels III–V retain supplied placement. Rights remain with their owners. Private decoded-frame inspection is not in-game placement/animation acceptance; graphical/playable delivery remains pending. |
| Quick-save/load controls and creature-status icons | Provisional | Provisional | The mod contains button states and ten new status icons under Mods/new-horizons/Images. These are user-authorized UI additions but no final-art or graphical acceptance evidence is recorded here. See [NH_USER_FEEDBACK.md](NH_USER_FEEDBACK.md) and the source files linked by the CSV. |

## UI surface coverage

UP-066's generic battle-form presentation is source/native verified:
the existing stack-status panel reads effective/original species, exact aggregate
creature HP and remaining form rounds. Its icon references the original creature's
native `CPRSMALL` portrait, not unrelated spell art or a fake spell marker.
Creature sprite replacement references the effective species' native animation
and is queued from unit-update/round events, preserving facing and inspected unit
identity. Both Linux targets build and four pure status/overflow checks pass
within the 51-case native retry. This is **Provisional** implementation with native-resolution rendering
and playable acceptance still pending. No newly created artwork or Final art
claim is involved. Purpose-made Polymorph spell artwork remains **Not done**, and
the spell is not enabled by this generic presentation slice.

The companion CSV has grouped rows for the main New Horizons surfaces requested for review: level-up skill/perk selection; the hero skill-odds pane; the parent-skill context shown by perk help; creature Speed, Initiative, Leadership requirement/capacity and stack-size readout; Orders selection/background/read-only tooltips; hero active effects and the typed Hero/Spell/Order action panel; battle log; Demonic Reserve and Gate indicators; and the main spellbook. Those implementation classifications are source-only, Provisional pending rendered review unless specifically marked Not done. The parent-skill help currently provides a text identity, not a separate custom parent-skill icon binding.

Legacy mastery/artillery UI references are not counted as live New Horizons 1.0 surfaces: [NH_VERSION_1_0_SCOPE.md](NH_VERSION_1_0_SCOPE.md) supersedes the earlier post-Expert mastery organization and scopes Siege as a rating, not a spendable Artillery resource. Existing historical code or files are not evidence of a requested live feature.

## Verified spellbook bindings

### Pending replacement-art integration

The table distinguishes installed source bindings from still-uninstalled
HoMM3-skill drafts. Existing live Provisional icons must not be mistaken for
the replacements that remain Not done.
The `output/homm3/` paths are local working outputs, not shipped assets.

| Requested replacement | Implementation | Art | Local evidence and remaining work |
| --- | --- | --- | --- |
| Metamagic Basic/Advanced/Expert | Provisional | Provisional | Canonical skill data selects the twelve `NH_metamagic_prism_*` assets. Basic/Advanced reuse the HoMM3-skill rank paintings; Expert has a targeted opaque-leather background repair. Masters, prompts, hashes and slot details are retained under `assets/new-horizons/art-source/metamagic-prisms-v2/`. Native32/44/82/58 reductions preserve shape; rectangular slots add transparent margins rather than stretching. Basic's slight original translucency is disclosed. In-game review and final user approval remain pending. |
| Hero Movement | Provisional | Provisional | The HoMM3-skill winged riding boot is bound as `NH_hero_movement_44` in the native 44px attribute slot and `NH_hero_movement_painted_32` in the growth window. Retained master, exact prompt and export hashes are under `assets/new-horizons/art-source/hero-attribute-replacements-v1/`. UP229 adds a read-only current land/sea capacity and cost-rule breakdown to the existing native popup, without new art or layout. Client/test builds,15 focused native cases and two source guards pass; text fit, localization and in-game visual acceptance remain open. This does not certify adventure-hover or per-tile factor presentation. |
| Hero Leadership | Provisional | Provisional | The HoMM3-skill command banner/gauntlet is bound as `NH_hero_leadership_44` in the main attribute pane and `NH_hero_leadership_24` in the compact legacy layout. The older `NH_capability_leadership` family has no current runtime consumer; creature and Fort Leadership use the separate crown below. Native exports are inspected; in-game visual acceptance remains pending. |
| Creature Leadership crown | Provisional | Provisional | The retained HoMM3-skill crown is bound as `NH_creature_leadership_20` in both the creature Leadership Cost row and ranked Fort cards. Source master, exact prompt and hashes are under `assets/new-horizons/art-source/creature-stat-glyphs-v1/`. Native export checks pass; in-game presentation remains unverified. |
| Creature rank staircase and stat row | Provisional | Provisional | The v2 stair-step glyph is bound as `NH_creature_rank_20` to a horizontal MainSection Rank row instead of a separate category section. Generated panels allocate the extra row only for categorized creatures; legacy uncategorized geometry is unchanged. Fort and recruitment surfaces intentionally use text headings/badges rather than this glyph. Source and art remain provisional pending rendered review. |
| Skill-probability information control | Provisional | Provisional | `HeroSkillOddsInfoMark` code-draws a transparent gold circled serif `i` over the four-state `NH_hero_growth_entry` control; it has no separate raster master or export. Left-click opens the skill-odds pane. Source placement checks pass, but centering, contrast, scaling, click area and tooltip behavior remain pending rendered/input review. |

Installation requires recorded provenance, retained source art, correctly sized
runtime exports, binding checks, and rendered review. Draft generation alone
does not complete any of these replacement requests.

The creature rank/crown integration passed the Linux `vcmiclient` build,
three glyph provenance/export tests, the creature rank and Initiative source
guards, and the recruitment-category source guard. Independent review found
no blocking issues. Source `df7e2cae6` was rebuilt with its committed identity,
frozen and promoted as snapshot
`ca6afa103919effdcb38e63d22105200d744cf6ac963732d5b3368977b28c8a0`.
The exact candidate completed 74 AI turns in a bounded 45-second headless
All for One smoke run (configured seed 1284510375; maximum completed turn
3823ms), with no checked crash, unsupported-rules or command-rejection errors.
The run ended at the planned timeout and left no client process. The normal
launcher's verify-only check passed. This confirms candidate startup/AI play,
not the creature panel's rendered appearance; GUI acceptance remains pending.

The hero Movement/Leadership replacement integration passed the Linux client
build, the focused hero-attribute binding/dimension guard, two master/export
provenance tests, and independent review. The broader level-up/art audit still
now passes its active-perk completeness gate: all 66 active perk IDs have named,
unique normal art and complete four-state descriptors. These
hero replacement assets remain provisional pending in-game visual review.
Source `359c0b509` was rebuilt, frozen and promoted as snapshot
`3ddeb77330ced93d2b8fb981fbe318068271ac7c100e7ca6f421e788da1d28b1`.
Its bounded 35-second headless All for One run completed 57 AI turns
(configured seed 1284510375; maximum completed turn 3960ms), with no checked
crash, unsupported-rules or command-rejection errors. Planned timeout completed
without a remaining client process; launcher verify-only passed. This is
startup/AI smoke evidence, not visual acceptance of the new icon bindings.

## Metamagic prism integration validation

The three rank paintings now supply all twelve skill image slots and the Basic
specialty images for Halon and Serena. The focused content, faction-art,
skill-entity, binding and pixel-export suite passes 39 tests; module regeneration
check and independent review pass. Square artwork is not stretched to fill the
rectangular slots. Expert's missing background is repaired; Basic retains its
original slight translucency.

The broader `nh-new-art-audit.py` still rejects unrelated unclassified PNG
families at its SVG/PNG inventory comparison. Its exact twelve-file prism
inventory check passes; this change does not relax the remaining inventory
requirement. Build, snapshot promotion and in-game visual acceptance are not
implied by these source checks. The artwork remains Provisional.

Source `e03005ae9` subsequently built successfully and was promoted as snapshot
`184dec2d8cb728229089ec477894a435e37e937ffdc545aa2ae413f39f07b3ad`.
The exact frozen candidate completed 57 AI turns in a 35-second isolated
headless check (configured seed 1284510375, maximum completed turn 3878ms).
No checked crash, unsupported-rule or command-rejection errors appeared;
the planned timeout left no client process. AI path-node allocation warnings
remain, so this is not a general performance clearance or visual acceptance.
The normal launcher's verify-only path check passes for this snapshot.

## Spellbook binding details

### Non-damage spell specialty tooltip conversion

Provisional presentation,2026-10-04: UP224 live-instance specialty descriptions
state the saved20% bonus to Cure's Spell Power-derived healing component.
Existing hero panels/icons remain unchanged; no new artwork or final visual
approval is claimed. Source review and builds3385/69675 pass; native20053
passes22/22 including saved component identity, not rendered panel acceptance.
Prototype-only specialty tooltip consumers and rendered layout remain Phase2.

### Creature-line specialty tooltip conversion

Provisional presentation,2026-10-04: hero and exchange live-instance tooltips
now describe the saved canonical +1 Speed/+1 Initiative and flat five-level
Attack/Defense growth capped6. Existing panels and artwork are unchanged.
UP216 native85455 passes7/7; this is source/native evidence, not rendered
approval. Prototype-only CHeroOverview/CKingdomInterface text remains a tracked
Phase2 discrepancy. No new art or final-art classification is implied.

### Specialty and Fort binding correction

Hero `specialtyLarge` images feed the 44×44 `UN44` atlas, not the 82×93
secondary-skill slot. All 21 authored New Horizons specialty bindings now use
their native 44×44 medium artwork; the four Necromancy specialties use the
corresponding classic `SECSKILL` frame. Skill-window large images are unchanged.
Fort Leadership Cost uses the same monochrome crown as the creature window,
retaining its existing row geometry. The 27 focused content/binding tests and
Fort source guard pass; independent review found no blockers. Native build and
playable promotion are tracked separately; these checks are not rendered acceptance.

The current mod.json binds, for each of Light, Nature, Sorcery, Havoc, Shadow, and Chaos:

- NH_<school>_bookmark.json, with selected/unselected frames.
- NH_<school>_header.png, a 160×96 page-one illustration, not a full-book splash.
- NH_<school>_spellBorders.json, four spell mastery frames: none/basic/advanced/expert.

The old school-art completeness note documented an earlier Sorcery border reuse. The current live mod.json now binds all six custom border descriptors; use the current source for this inventory. The additional emblem/button outputs described by the school-family specification are not registered in SpellSchoolHandler today.

Time Stop is in the active roster at Mods/new-horizons/mod.json under settings.magic.newHorizons.spells. Its current definition in Mods/new-horizons/Content/config/spells/newHorizons.json sets iconBook to SPELLS.def:0:15 (and matching spellbook/bonus icon frames). config/spells/offensive.json identifies Magic Arrow as spell index 15, and lib/constants/EntityIdentifiers.h assigns MAGIC_ARROW = 15. This verifies the current borrowed binding; it does not assert a live screenshot or any other runtime behavior. Counterspell, Disintegrate, Focus Magic, and Phantom Army also use frame 15. A dedicated, role-appropriate icon is still required for all five spells.

The combat-spell settings contain 78 entries, of which 77 are active after excluding inactive `core:clone`. This count is a settings inventory, not proof that every icon or effect is final. The other new spell icon slots are source-confirmed: Master Chain Lightning uses frame 19, matching the original Chain Lightning index; Transfigure Matter uses frame 64, matching Remove Obstacle. Both remain Provisional as New Horizons spell art until visually approved. Holy Wrath has a purpose-made provisional icon family but still borrows the scenario-bonus frame. Storm of Daggers has a purpose-made provisional icon family and per-target impact animation; Regeneration, Holy Armor, and Life Drain have purpose-made provisional icon families. Native visual approval remains open. Magic Missile remains unregistered and is not counted as a live spell.

Perk and skill manifests preserve source files, runtime hashes, and export bindings. Relevant files include [NH_skill_art_manifest.json](../assets/new-horizons/art-source/NH_skill_art_manifest.json), [faction_skill_icon_manifest.json](../assets/new-horizons/art-source/skill-icons/faction_skill_icon_manifest.json), and the runtime manifests for [active-perks-v1](../assets/new-horizons/art-source/active-perks-v1/runtime-manifest.json), [active-perks-v2](../assets/new-horizons/art-source/active-perks-v2/runtime-manifest.json), [active-perks-v3](../assets/new-horizons/art-source/active-perks-v3/runtime-manifest.json), [active-perks-v4](../assets/new-horizons/art-source/active-perks-v4/runtime-manifest.json), [active-perks-v5](../assets/new-horizons/art-source/active-perks-v5/runtime-manifest.json), [Sylvan Luck v1](../assets/new-horizons/art-source/sylvan-luck-perks-v1/runtime-manifest.json), [Sylvan Luck v2](../assets/new-horizons/art-source/sylvan-luck-perks-v2/runtime-manifest.json), [Warcasting](../assets/new-horizons/art-source/warcasting-perks-v1/runtime-manifest.json), [Battle Meditation](../assets/new-horizons/art-source/battle-meditation-v1/runtime-manifest.json), and [Chain Gate](../assets/new-horizons/art-source/chain-gate-v1/runtime-manifest.json). These are technical/provenance records; they do not imply visual approval. The eight per-Order art manifests are under [orders-v1](../assets/new-horizons/art-source/orders-v1/).

## Maintaining this register

Update the CSV from these sources when bindings or active status change:

- Spells and school assignments: Mods/new-horizons/mod.json; spell graphics: Mods/new-horizons/Content/config/spells/newHorizons.json and config/spells/*.json.
- Skill/rank bindings: Mods/new-horizons/mod.json; skill/perk catalogue and active/planned effect status: config/newHorizonsPerks.json.
- Perk icon lookup: client/windows/NewHorizonsPerkIcons.h; authored perk runtime files/manifests under assets/new-horizons/art-source/.
- Orders: Mods/new-horizons/mod.json and client/battle/BattleHeroActionWindow.cpp; outputs under Mods/new-horizons/Images and assets/new-horizons/art-source/orders-v1/.
- Spellbook tabs/borders/header: Mods/new-horizons/mod.json, lib/spells/SpellSchoolHandler.cpp, and client/windows/CSpellWindow.cpp.
- Hero/recruitment/convenience UI: source links recorded in the corresponding CSV rows.

This register is documentation only; it creates no artwork. Any future new or revised HoMM3 raster art, including provisional concepts, must use the `$homm3-art` skill and follow [NH_APPROVED_ART_WORKFLOW.md](NH_APPROVED_ART_WORKFLOW.md); the workflow's process approval is not blanket asset approval.

Keep the three status values exact. Record implementation evidence separately from art approval. Preserve existing Final classifications when unrelated art families change.
