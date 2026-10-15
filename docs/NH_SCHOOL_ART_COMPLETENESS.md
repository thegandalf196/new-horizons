# School artwork consumer contract

All six current school definitions in `Mods/new-horizons/mod.json` bind dedicated `NH_<school>_spellBorders.json`, header and bookmark resources. The historical Sorcery `SplevA` fallback is not current. Selected bytes/approval live in the runtime manifest; source bindings alone do not certify every rendered state.

## Family checklist

| Role | Native contract |
| --- | --- |
| Basic/Advanced/Expert Skill | 32×32, 44×44, 82×93 and 58×64 per rank: 12 outputs |
| Spell proficiency overlays | Four transparent 78×65 frames, proficiency 0/1/2/3 |
| Bookmark | Selected/unselected 80×60; also review compact 68×51 display |
| Header | 160×96, top 28 transparent; page 0 only |
| Emblem | 64×64 |
| Button | Four 64×64 normal/pressed/disabled/highlighted states |

The family accounts for 24 image outputs, excluding descriptors and individual spell families. This is a consumer checklist, not 24 independent concepts or an approval count. Proficiency corners add bottom-left, top-left, top-right, bottom-right in order; include frame0 even though Skills have three ranks.

`SpellSchoolHandler` loads borders; `CSpellWindow::SpellArea::setSpell` chooses effective school proficiency. All-tab uses the spell's resolved school; a selected tab uses that tab. Preserve original DEF placement, PNG centering, unobstructed spell symbol and overlay offsets.

Review all ranks/states, page/All/selected tabs, native/default/compact layout, alpha/contrast and provenance. Spell symbols are transparent subjects; UI parchment/leather is a separate layer. See [asset specifications](NH_ART_ASSET_SPECIFICATIONS.md), [template workflow](NH_TEMPLATE_ART_WORKFLOW.md) and [UI guide](NH_HOMM3_UI_STYLE_GUIDE.md).
