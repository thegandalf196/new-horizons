# Canonical design-source migration to Markdown

On 2026-09-27 the user chose the repaired `design-sources/New Horizons.md` as
the sole gameplay specification. `New Horizons.docx` is retained as the archival
conversion baseline, not a parallel authority. Unintegrated decisions belong
only in `NEW_HORIZONS_PENDING_CHANGES.md`.

## Source identities

| Artifact | SHA-256 | Role |
|---|---|---|
| `design-sources/New Horizons.docx` | `7c7c6c1a45c3b4ee4a3c5c625dd1b1d10eb46acdb6029aedfe8735a523c1108a` | Historical comparison source |
| `design-sources/New Horizons.md` | `b9c3b35495bea2ba3f26d2b6d2529b6051bdd30a96c618df9f9071d28a250ee0` | Canonical source at this migration checkpoint; later amendments change its hash |

## Conversion audit and repairs

The original Markdown export was not adopted without review. We compared the
DOCX's 126 tables and 1,090 rows with the Markdown's 133 pipe-table blocks.
The different block count reflects pagination/split tables and is not a
one-to-one fidelity metric. Every Markdown table was checked for consistent
column width. All 31 Skill-perk pools contain ten rows each (310 total), with
no unapproved normalized cell differences from the DOCX.

Repairs include the nine missing Necromancy perk rows, the Rampart Centaur
Leadership row, split Orders/Havoc/Adventure Spell/Mage/Arch Mage/town tables,
and the class-weight header that had absorbed Knight's data. A second full-table
review restored Light's Crusade and Chaos's two Level-5 rows, Fortress/Conflux
Guild depth, Nature's Mana header, the attribute-scale and Ranger profile
tables, the UI Hero Action row, and five entirely flattened numeric/progression
tables (Shadow Gift, Sorcery targets, Pandemonium, Nature and Havoc). Reordered
Luck/Morale prose was returned to the DOCX wording. The repaired Markdown has
136 pipe-table blocks with consistent widths. The five Adventure Spells, Flank
and Second Wind Orders, Mage/Arch Mage stat rows, and all 18 class rows in each
weight matrix were checked. Inherited trailing export whitespace was normalized
without changing rule text.

Final independent review found and corrected one further conversion token:
Shield of Chaos resists physical and magical damage generally, not only Chaos
damage. No other unexplained missing table-cell text was found in the full
normalized DOCX comparison.

The user-approved Bulwark clarifications were incorporated into the Markdown:
Toxic Spines is first-attacker-per-Defending-stack, with reflection-based,
three-activation physical Poison that does not stack; equal or stronger
reapplication refreshes it, weaker reapplication is ignored, Cure removes it,
and ordinary Dispel does not. Immovable applies only while that Bulwark stack
is Defending. These are intentional newer decisions, not DOCX transcription
differences. The generated perk registry and curated module use the Markdown
hash and descriptions; source-derived tests compare all 310 rows.

The older School Skill summary and six repeated rank descriptions said spells
could be "learned and cast" only with the corresponding School rank. These were
reconciled with the later user-approved rule already in the DOCX: School rank
gates ordinary acquisition, not casting of a legitimately inscribed spell.
Starting and explicitly temporary inscriptions remain usable without the rank.

The DOCX's embedded Orders illustration was not exported or used as game art.
It remains subject to provenance and redistribution review. Historical DOCX
links in dated migration records describe past checkpoints only; they confer
no current authority.

The former DOCX-rewrite workflow is retired. Do not rewrite the archival DOCX
for new amendments or introduce a second canonical source.

## Validation boundary

The maintained `tools/tests/test_new_horizons_perk_data.py` guards source identity,
31/310 catalogue comparison, approved Bulwark differences, non-perk anchors and
table widths. Run it with the verified configured interpreter. Historical passing
checks establish source/catalogue migration, not gameplay implementation, native
build or playable appearance.
