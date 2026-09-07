# Original convenience and creature-trait artwork

## Scope and provenance

User confirmed F8/F9 work and authorized adding visible quick-save/load buttons
and creature ability/status artwork. This increment does not repair or replace
save/load rules. It must retain the existing shortcuts, validation and unavailable
states. Build owns the small additive widget/configuration changes.

`generate_convenience_icons.py` is a NEW, independent generator. It does not import,
execute or change the held mastery generator, its art, or a released candidate.
The illustrations are newly authored layered vector geometry: an engraved book
with inward/outward arrow for save/load, and shaded pictograms for creature traits.
They are AI-assisted original artwork, not commissioned paintings or a claim that
all requested visual polish is finished. Readable existing descriptions remain
necessary; icons alone are not gameplay explanations.

The generator and its new SVG/PNG outputs are dedicated under **CC0-1.0**:
https://creativecommons.org/publicdomain/zero/1.0/legalcode . VCMI product code
retains its existing GPL license. No additional rights in Heroes, VCMI Extras,
WoG or user-supplied concept images are asserted.

No Extras artwork/configuration was copied, traced or transformed. Public Extras
metadata was inspected only to identify the presentation difference. Author credits
and public availability do not establish redistribution permission; no such
permission is presumed. No purchaser art, fonts or external images are inputs to
this generator. Its SVGs retain editable geometric layers and explicit colors.
This provenance statement is not a guarantee about all legal aspects of a release.

## Output contract

Button animation stems: **NH_qsave_24**, **NH_qload_24**.
Each has four24x24 RGBA PNG frames and matching SVGs, in engine order:
0 normal,1 pressed,2 blocked/disabled,3 highlighted. Separate animation JSONs
reference those files. Pressed shifts the inner artwork; disabled desaturates it;
hover brightens the rim. Neither button is a toggle or a gameplay-state mutation.

Proposed placement, independently authored against the current core layout:
container top171/right69/width56/height24, hidden in world-view mode; child buttons
at left0 and32. The minimap ends at170; existing lists/buttons start196. Static
fit is not rendered acceptance. Build adds only this small container, not an
imported or replacement adventure-map layout. Existing command/help identifiers:
`adventureQuickSave`, `adventureQuickLoad`. The STRING help values are PREFIXES
`vcmi.adventureMap.quickSave` and `vcmi.adventureMap.quickLoad`: readHintText appends
`.hover` and `.help` to resolve their two translated leaf keys. Do NOT pass a
`.help` leaf as the prefix. Use playerColored=false for these true-color images.

The first947 GUI exposed this source-review miss: the incorrect `.help` prefix
rendered raw `vcmi.adventureMap.quickSave.help.hover`. Real button quick-save
succeeded, but quick-load/cards were not exercised before the run stopped. Build
corrects only the two prefix strings and tests actual suffix resolution plus
rejection of the old leaf-as-prefix. Frozen947 and prior42/46 reports remain
preserved RED history; a new freeze and actual hover/help/input journey are required.

Static creature icons are50x50 RGBA PNGs with matching SVGs. Build confirmed the
following descriptor mappings; no values, flags, creatures, spells or mechanics
are changed. All patches are graphics-only except an explicitly authored Siege
Weapon presentation description, documented below:

| Stem (NH_status_…_50) | Core bonus |
| --- | --- |
| undead | UNDEAD |
| flying | FLYING |
| shooter | SHOOTER |
| siege | SIEGE_WEAPON |
| noRetaliation | BLOCKS_RETALIATION |
| unlimitedRetaliations | UNLIMITED_RETALIATIONS |
| breath | TWO_HEX_ATTACK_BREATH |
| adjacent | ATTACKS_ALL_ADJACENT |
| resistance | MAGIC_RESISTANCE |
| regeneration | HP_REGENERATION |

The first mapping proposal incorrectly used NO_RETALIATION, which prevents the
subject unit from retaliating (for example paralysis). BLOCKS_RETALIATION is the
common Naga/Vampire ability that prevents enemy retaliation; it uses the existing
noRetaliation artwork. Preserve NO_RETALIATION and its spell presentation unchanged.
The old42 offer and resolver results remain retained semantic-RED evidence.

SIEGE_WEAPON has an empty default description. The creature window deliberately
omits blank/hidden entries even when an icon resolves. Build is authorized to add
this original presentation-only description, without changing that filter:

```
{Siege Weapon}
This unit has the siege-weapon trait.
```

This is an explicit exception to description identity; other existing descriptions
and all nature/hidden/value/subtype semantics must remain intact. Ten resolved
images do not establish ten visible traits. Corrected native-loader checks and
actual creature-window journeys are still required before visibility claims.

Existing custom bonus icons retain precedence. Resistance is not immunity;
regeneration is not resurrection; breath artwork does not declare a damage school.
These are ten common traits, not exhaustive artwork for every engine bonus.

## Generation and validation

After any quiet GUI lease ends:

```
python3 assets/new-horizons/generate_convenience_icons.py
```

For isolated reproduction, use `--output-root <temporary-directory>`. Requires
Pillow only, no compiler or GUI. Expected new outputs:18 SVGs,18 PNGs,2 animation
JSONs. The old421 outputs must remain byte-identical. This generator writes only
its new unique names; it neither deletes nor rewrites other families.

After the first mastery GUI teardown,38 outputs were generated and reproduced
byte-identically in an isolated output root; all421 prior output hashes remained
unchanged (459 total). Producer size/RGBA/geometric-SVG/state-order checks passed;
a native-size contact sheet was inspected. The separate
`audit_convenience_icons.py` also checks visible trait edge clearance and rejects
six synthetic negatives: blank, wrong size, opaque, visible edge contact, swapped
animation roles and embedded/external SVG imagery. Local evidence is under ignored
`build/new-horizons-linux/research/convenience-art/`. Independent asset review is
still required; the generator and auditor are both original CC0 source.

The independently authored optional UI hook appends only the items of an active
`config/widgets/nhConvenience.json` resource after the base layout is built. It
neither replaces that layout nor imposes NH image dependencies when the module is
disabled. Portrait layout is deliberately excluded until separately reviewed.
Build owns the fragment, its namespaced widget names, mounting and bonus mappings.
An initial source-review RED found that passing a bare items array to build() would
assert on object-level dimensions in Debug. The repaired hook copies only items
into a sanitized JsonNode OBJECT before build(); it never passes the whole fragment.
This finding is retained, not retroactively reported as a successful initial hook.

No compiled-hook, layout/input, quick-load-button, bonus-icon rendering, Windows or
final artistic acceptance is implied by generated files. Build and sole Content
Tester own integrated freeze/normal-input validation. Subsequent generation and
audits remain paused whenever a quiet GUI lease is active.
