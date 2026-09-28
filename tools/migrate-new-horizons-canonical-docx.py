#!/usr/bin/env python3
"""Apply approved legacy-rule migrations to the canonical New Horizons DOCX."""

from __future__ import annotations

import copy
import shutil
import tempfile
import zipfile
from pathlib import Path

from lxml import etree


ROOT = Path(__file__).resolve().parents[1]
DOCX = ROOT / "docs/design-sources/New Horizons.docx"
W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
NS = {"w": W}


REPLACEMENTS = {
    "Normally, once per combat round, a hero uses one Hero Action to cast one Spell or issue one Order. Exceptional abilities such as Metamagic, Divine Mandate, and Double Command state their additional action directly; New Horizons does not create action tokens or a separate action currency.":
        "Normally, once per combat round, a hero uses one Hero Action to cast one Spell or issue one Order. Exceptional abilities such as Metamagic, Divine Mandate, and Double Command state their additional Spell-only or Order-only opportunity directly; these typed restrictions are contextual allowances, not a general action-token currency or three independent counters. Authoritative validation, spending, expiry, AI projection, logs, and save/load must agree, and a Creature Activation never spends a hero allowance.",
    "Creature Speed and Initiative are separate combat statistics. Speed determines battlefield movement range during a Creature Activation. Initiative determines when that activation occurs. Neither statistic determines the hero's daily adventure-map Movement.":
        "Creature Speed and Initiative are separate combat statistics. Speed determines battlefield movement range during a Creature Activation. Initiative determines when that activation occurs. Neither statistic determines the hero's daily adventure-map Movement. Slow reduces Initiative only; Vengeful Vines may reduce Speed. Mage and Arch Mage both have Speed 5, with Initiative 5 and 7 respectively. The retired Frost Bolt Speed-debuff rule does not transfer to Ice Bolt: the current Ice Bolt explicitly changes neither Speed nor Initiative.",
    "The three-perk limit is independent of this sequence. A hero may learn additional perks from a tier for which they are eligible, but doing so consumes one of that Skill's three perk slots. The normal progression requires at least one Basic perk before Advanced rank and at least one Advanced perk before Expert rank; it does not require that the hero learn exactly one perk at each tier.":
        "Each Skill has exactly one perk slot at each tier: one Basic perk, one Advanced perk, and one Expert perk. A hero cannot spend multiple perk slots in the same tier. Normal progression therefore follows Basic Skill -> one Basic perk -> Advanced Skill -> one Advanced perk -> Expert Skill -> one Expert perk. Exceptional rank advancement may bypass the Skill-rank step, but it never bypasses this perk order: missing perk tiers must still be filled Basic, then Advanced, then Expert.",
    "Knowledge determines maximum Mana directly:":
        "Knowledge determines the hero's normal Spell Point capacity directly:",
    "Maximum Mana = Knowledge":
        "Maximum Normal Spell Points = effective Knowledge",
    "Mana itself is a Statistic, not an Attribute.":
        "Spell Points are a Statistic, not an Attribute. Normal and Buffer Spell Points are tracked separately but spent as one currency.",
    "This makes Knowledge particularly transparent: one point of Knowledge equals one point of maximum Mana.":
        "This makes Knowledge particularly transparent: without Intelligence, one point of effective Knowledge equals one point of normal Spell Point capacity.",
    "Polarity rule: pure Might Skill weights sum to 5 x (Attack growth + Defense growth), while pure Magic Skill weights sum to 5 x (Spell Power growth + Knowledge growth). Because every Primary Attribute growth vector totals 10, each class therefore has a fixed 50-point Might/Magic Skill budget that exactly follows its Primary Attribute polarity. Warcasting is outside both budgets and depends on balance: 8:2 or 2:8 -> 1; 7:3 or 3:7 -> 4; 6:4 or 4:6 -> 7; 5:5 -> 10. Strategic Skills are weighted independently by class fantasy. The hero's own Faction Skill has weight 10; all other Faction Skills are ineligible. Command is ineligible for Magic classes and Wisdom is ineligible for Might classes.":
        "The authored class tables below are the authoritative relative Skill weights. The former formula derived from ten-point Primary growth and a fixed 50-point Might/Magic budget is retired; changing the current 20-point growth vectors does not silently recalculate these tables. Primary growth still informs class identity, but every weight change must be authored explicitly. Ordinary offers use the eligible weighted pool without a forced Wisdom or Magic School cadence. Strategic Skills remain independently weighted by class fantasy. The hero's own Faction Skill has weight 10; all other Faction Skills are ineligible. Command is ineligible for Magic classes and Wisdom is ineligible for Might classes.",
    "Maximum Mana increases by 25% of Knowledge, rounded down.":
        "Maximum Normal Spell Points equal floor(1.30 × effective Knowledge). Intelligence is the sole general multiplicative capacity effect.",
    "Uses the existing Wall of Knowledge graphics. Once per week, the Reservoir may raise one visiting hero's current Mana to twice that hero's normal maximum. Normal regeneration cannot refill above the normal maximum.":
        "Uses the existing Wall of Knowledge graphics. Once per week, the Reservoir grants one visiting hero +50 Buffer Spell Points. It does not refill missing Normal Spell Points, alter Knowledge, or change maximum capacity.",
    "Uses the existing Mana Vortex graphics: the visual remains a luminous, cosmic swirling phenomenon rather than becoming a brick-built hall. Each individual Astral Nexus grants a visiting hero +5 permanent Knowledge once.":
        "Uses the existing Mana Vortex graphics: the visual remains a luminous, cosmic swirling phenomenon rather than becoming a brick-built hall. Whenever a hero visits, the Astral Nexus automatically restores that hero's Normal Spell Points to the current maximum. It grants no Buffer Spell Points and has no per-hero or weekly use limit.",
    "Magic University":
        "House of Wisdom",
    "This arrangement results in a balanced distribution. Nature and Havoc each appear in four factions, making them the most widespread traditions. Shadow and Sorcery each appear three times, while Light and Chaos appear twice each. Tower is the most magic-centered town overall: its Sorcery/Havoc identity, hero development, and unique arcane infrastructure should make it the clearest specialist in formal magic. Conflux is its closest rival in magical infrastructure, especially through the Magic University. Castle remains the principal center of Light magic rather than an equal general-purpose arcane center.":
        "This arrangement results in a balanced distribution. Nature and Havoc each appear in four factions, making them the most widespread traditions. Shadow and Sorcery each appear three times, while Light and Chaos appear twice each. Tower is the most magic-centered town overall: its Sorcery/Havoc identity, hero development, and unique arcane infrastructure should make it the clearest specialist in formal magic. Conflux is its closest rival in magical infrastructure, especially through the House of Wisdom and its spell-scroll economy. Castle remains the principal center of Light magic rather than an equal general-purpose arcane center.",
    "Tower is the most magic-centered town overall. Its Sorcery/Havoc school pairing, Metamagic, Library, Mana infrastructure, artifact economy, and scholarly buildings should make it the clearest civilization of formal magical study. Conflux is the closest rival, but expresses magical breadth through the Magic University and elemental identity rather than duplicating Tower's institutions.":
        "Tower is the most magic-centered town overall. Its Sorcery/Havoc school pairing, Metamagic, Library, Mana infrastructure, artifact economy, and scholarly buildings should make it the clearest civilization of formal magical study. Conflux is the closest rival, but expresses magical breadth through the House of Wisdom's spell-scroll economy and elemental identity rather than duplicating Tower's institutions.",
    "A hero with an available Skill slot may pay to learn Basic rank in any one of the six Magic School Skills. A hero may return later to purchase another school if another Skill slot remains available and the price is paid; there is no once-per-scenario cap.":
        "Each House of Wisdom generates a persistent stock of six distinct eligible combat-spell scrolls. Visiting heroes may buy an offered scroll for 1,000 Gold per spell level; a purchased scroll is removed from that House's stock. It teaches no Skill and never offers Adventure Spells, removed spells, or spells prohibited by the saved roster or map.",
    "Magic University tuition per Basic Magic School Skill is 5,000 Gold + 2 Mercury + 2 Sulfur + 2 Crystal + 2 Gems. The hero must have a free Skill slot.":
        "House of Wisdom scrolls cost 1,000 Gold per spell level. Each House has six distinct eligible combat-spell offers, generated once and preserved in saves; purchasing one removes it from that House's stock.",
    "Unless this document explicitly changes a creature, its Gold/resource recruitment cost and dwelling/upgrade construction cost begin from Heroes III Complete values. Renamed unique buildings retain the construction cost of the graphic/building they replace for the first test pass. This isolates the effect rebalance from the economy rebalance. The explicit exceptions are the Tower Mage/Genie changes, the Castle Griffin/Swordsman swap, new Mage Guild IV/V costs, Adventure Spell unlocks, and Magic University tuition listed above.":
        "Unless this document explicitly changes a creature, its Gold/resource recruitment cost and dwelling/upgrade construction cost begin from Heroes III Complete values. Renamed unique buildings retain the construction cost of the graphic/building they replace for the first test pass. This isolates the effect rebalance from the economy rebalance. The explicit exceptions are the Tower Mage/Genie changes, the Castle Griffin/Swordsman swap, new Mage Guild IV/V costs, Adventure Spell unlocks, and House of Wisdom scroll prices listed above.",
    "If the Gate is placed adjacent to another friendly Inferno stack, the gated stack gains +2 Initiative during its first round on the battlefield.":
        "If the Gate is placed adjacent to another friendly Inferno stack, the gated stack gains +2 Initiative during its first actionable round. A delayed arrival never consumes the bonus before the gated stack can act.",
    "After selecting the target, a small Overcharge panel appears adjacent to the spell cursor/card. It contains a segmented Mana slider, − and + controls, and live previews for total Mana cost and final damage.":
        "After selecting the target, a compact centered Overcharge modal appears in the Heroes III leather, red, and gold visual style. It contains a segmented Mana slider, − and + controls, and live previews comparing total Mana cost, final damage, and estimated casualties with and without the selected Overcharge.",
    "After selecting a target, open a compact segmented Overcharge panel beside the cursor/card: - and + controls or a slider, current Overcharge, maximum allowed by Spell Power, base Wisdom-adjusted Mana cost, additional Overcharge Mana, total Mana, and live projected damage. Disable unaffordable values before confirmation.":
        "After selecting a target, open a compact centered segmented Overcharge modal in the Heroes III leather, red, and gold visual style: - and + controls or a slider, current Overcharge, maximum allowed by Spell Power, base Wisdom-adjusted Mana cost, additional Overcharge Mana, total Mana, and live projected damage and estimated casualties both with and without the selected Overcharge. Disable unaffordable values before confirmation.",
    "Keep the existing six-school bookmarks and new school artwork. Add a separate neutral Adventure Spell section rather than treating Adventure Magic as a seventh school. Extend spell entries with school-rank locks, Wisdom-adjusted ordinary spell costs, Mass-variant availability, special casting controls, and richer targeting feedback.":
        "Keep the existing six-school bookmarks and new school artwork. Add a separate neutral Adventure Spell section rather than treating Adventure Magic as a seventh school. Extend spell acquisition offers with School-rank requirements, and extend inscribed spell entries with Wisdom-adjusted ordinary spell costs, Mass-variant availability, special casting controls, and richer targeting feedback. Never present insufficient School proficiency as a casting lock on a legitimately inscribed combat spell.",
    "Keep the existing six-school bookmarks and new school artwork. Add a separate neutral Adventure Spell section rather than treating Adventure Magic as a seventh school. Extend spell acquisition offers with School-rank requirements, and extend inscribed spell entries with Wisdom-adjusted ordinary spell costs, Mass-variant availability, special casting controls, and richer targeting feedback. Never present insufficient School proficiency as a casting lock on a legitimately inscribed combat spell.":
        "Keep the existing six-school bookmarks and new school artwork. Add a separate neutral Adventure Spell section rather than treating Adventure Magic as a seventh school. Extend spell acquisition offers with School-rank requirements, and extend inscribed spell entries with Wisdom-adjusted ordinary spell costs, Mass-variant availability, special casting controls, and richer targeting feedback. Never present insufficient School proficiency as a casting lock on a legitimately inscribed combat spell. The player may always open and inspect the spellbook after casting opportunities are exhausted; inspection never authorizes an illegal cast, automatically reopens the book, or repurposes Wait as a decline control.",
    "Spell entries show the School Skill rank required for Levels 3 / 4 / 5. If a spell is known or visible but the hero lacks the required rank, show it as locked with the exact requirement rather than silently hiding it.":
        "Spell acquisition offers show the School Skill rank required for Levels 3 / 4 / 5. If an unknown spell is visible from a Guild, teacher, scroll source, or other acquisition interface but the hero lacks the required rank, show why it cannot be acquired rather than silently hiding it. A legitimately inscribed combat spell is never casting-locked by insufficient School proficiency.",
    "Show listed Mana cost and current final casting cost when Wisdom or other cost modifiers apply. Tooltips should break down the calculation. Wisdom reduces spell cost; School Skills unlock spell levels.":
        "Show listed Mana cost and current final casting cost when Wisdom or other cost modifiers apply. Tooltips should break down the calculation. Wisdom reduces spell cost; School Skills unlock spell levels for acquisition but never revoke casting access to legitimately inscribed combat spells.",
    "Hero Action state; complete Orders panel/targeting/state feedback; initiative integration; school-rank locks; Wisdom cost display; Mass variants; Magic Arrow Overcharge; special spell targeting and previews.":
        "Hero Action state; complete Orders panel/targeting/state feedback; initiative integration; School-rank acquisition feedback without known-spell casting locks; Wisdom cost display; Mass variants; Magic Arrow Overcharge; special spell targeting and previews.",
    "Keep the current redesigned layout. Populate the complete Skill roster, Basic / Advanced / Expert rank, acquired perks, the three-perk limit, class/faction restrictions, and Faction Skill. No new top-level Hero-screen redesign is required.":
        "Keep the current redesigned layout. Populate the complete Skill roster, Basic / Advanced / Expert rank, exactly one perk slot per tier, class/faction restrictions, and Faction Skill. The Skill-probability pane uses Skill icons, excludes other factions' unique Skills, and opens from a small gold circled serif information control beside the Skills / learned perks heading with leather showing through and no rectangular button background. Stormcaller, Movement, hero Leadership, and creature Leadership use purpose-appropriate visible icons. No new top-level Hero-screen redesign is required.",
    "Add Leadership per creature and total Leadership for an inspected stack. Preserve existing useful statistics rather than shrinking them to make room.":
        "Show Speed and Initiative as separate full rows. Show Leadership Cost as a full row with its simple yellow monochrome crown icon, and show the inspected stack's current size / maximum commandable count such as 5 / 11 while keeping per-creature and total-stack Leadership costs distinct. Core / Elite / Champion is a horizontal stat row with a simple yellow monochrome ascending stair-step-line icon, not a ladder with rails and rungs. Preserve existing useful statistics, readable icon/label/value spacing, and panel styling rather than shrinking them to make room.",
    "Show whether the hero's normal Hero Action for the round is available or already spent. Do not introduce a general action-token currency. Automatic extra Spell or Order opportunities granted by specific rules must be shown as contextual extensions of the existing Spell or Order controls.":
        "Show whether the hero's normal Hero Action for the round is available or already spent. Do not introduce a general action-token currency or a panel presenting Hero, Spell, and Order as three independent counters. Automatic extra Spell or Order opportunities granted by specific rules are contextual extensions of the ordinary Spell or Order controls; show their source and expiry there. Generic Skill resources or states use the provider-driven Faction Skill status presentation. The panel uses surrounding leather, red outlines, gold detailing, and readable spacing rather than a bare box.",
    "Provide finished icons for all eight Orders, current calculated numerical effect using the hero's ratings and Command, duration, target type, and disabled-state explanation. Targetless army-wide Orders should resolve directly after confirmation; targeted Orders enter targeting mode.":
        "Provide finished icons for all eight Orders, current calculated numerical effect using the hero's ratings and Command, duration, target type, and disabled-state explanation. The combat Orders control uses a readable monochrome yellow gauntlet; the chooser uses Heroes III textured leather rather than a flat grey panel. Disabled Orders remain right-click inspectable. Targetless army-wide Orders resolve after confirmation; targeted Orders enter battlefield targeting mode rather than opening a second target menu.",
    "Show compact status badges with source and duration. Charge! should indicate whether a stack's qualifying first attack is still available; Hold the Line! should mark the anchored position; Brace! should show readiness; Protect! should visually link Protector and Ward; Focus Fire! / Flank! should mark the designated enemy.":
        "Show compact status badges with source and duration. Charge! should indicate whether a stack's qualifying first attack is still available; Hold the Line! should mark the anchored position; Brace! should show readiness; Protect! should visually link Protector and Ward; Focus Fire! / Flank! should mark the designated enemy. Orders are not dispellable spells and do not inherit spell-duration modifiers. Heroes likewise show active-effect states such as Warcasting. Reuse suitable existing animations but distinguish different Orders; exact per-Order assignments remain implementation judgment.",
    "After selecting a target, open a compact centered segmented Overcharge modal in the Heroes III leather, red, and gold visual style: - and + controls or a slider, current Overcharge, maximum allowed by Spell Power, base Wisdom-adjusted Mana cost, additional Overcharge Mana, total Mana, and live projected damage and estimated casualties both with and without the selected Overcharge. Disable unaffordable values before confirmation.":
        "After selecting a target, open a compact centered segmented Overcharge modal in the Heroes III leather, red, and gold visual style: - and + controls or a slider, current Overcharge, maximum allowed by Spell Power, base Wisdom-adjusted Mana cost, additional Overcharge Mana, total Mana, and live projected damage and estimated casualties both with and without the selected Overcharge. Recalculate from the shared battle forecast whenever Overcharge changes, using current target health, partial casualties, temporary HP, resistance, and mitigation. Disable unaffordable values before confirmation and label uncertain outcomes as estimates rather than guaranteed kills.",
    "Teleport highlights every legal destination. Blink previews its possible radius rather than pretending the destination is deterministic. Transfigure Matter highlights valid obstacles and previews the resulting summon HP / count before confirmation.":
        "Teleport highlights every legal destination. Blink previews its possible radius rather than pretending the destination is deterministic. Transfigure Matter highlights valid obstacles and previews the resulting summon HP / count before confirmation. Until purpose-made final art is approved, Transfigure Matter deliberately uses Remove Obstacle's spell icon rather than an unrelated placeholder.",
    "Use the existing Hero screen to show acquired / available / locked perks, the three-perk maximum per Skill, and why a perk is locked. School Skill entries should explicitly state the maximum spell level currently unlocked.":
        "Use the existing Hero screen to show acquired, available, and locked perks, with one empty visual slot for each unfilled Basic, Advanced, or Expert tier and an explanation of every lock. Left-clicking a Skill opens a read-only browser of all ten perks grouped by tier with names and icons, visibly distinguishing learned and unlearned entries; right-clicking any entry, including an unlearned one, shows the ordinary perk explanation with the owning Skill's name and icon. Browsing never acquires a perk. School Skill entries explicitly state the maximum spell level currently unlocked for acquisition.",
    "Creature info shows Leadership per creature. Stack inspection shows stack total. Recruitment and army exchange screens show the receiving hero's per-slot capacity, additional Leadership required by the proposed stack, resulting legal maximum stack size, and a precise explanation when a transfer/recruitment would exceed capacity.":
        "Beside the hero's current Leadership total, show +x for the class's per-level Leadership growth; artifacts and temporary bonuses affect the total but not this growth annotation. Creature info shows Leadership per creature, total stack Leadership, and current stack size / maximum commandable count. Recruitment and army exchange screens show the receiving hero's per-slot capacity, additional Leadership required by the proposed stack, resulting legal maximum stack size, and a precise explanation when a transfer or recruitment would exceed capacity.",
    "Town and external-dwelling recruitment consistently present the three creature categories and any tier-sensitive Skill effects such as Recruitment Muster or faction mechanics.":
        "Town recruitment presents the complete authored roster simultaneously in horizontal Core, Elite, and Champion bands and adapts each row without dropping, merging, or duplicating creatures to force a universal 3 / 3 / 1 layout. Each card shows name, portrait, available count, weekly growth, Attack, Defense, Damage, Health, Speed, Initiative, Leadership Cost, and Growth with the creature UI's statistic icons; dwelling previews and dwelling names do not appear. Increase the window and cards as needed for readable rows, while retaining the town's Heroes III identity, resource bar, date, and confirmation control. Core, Elite, and Champion headings use the same yellow font in every town and fallback Fort screen. Correct Conflux-specific crowding and clipped previews without redesigning working town layouts. External dwellings also present the category and any tier-sensitive Skill effects such as Recruitment Muster or faction mechanics.",
    "Spellbinder's Hat is treated as a combat-magic relic: it allows the hero to cast combat spells already present in the spellbook without School proficiency restrictions, but it teaches no spells and never grants or bypasses Adventure Spells. The four legacy elemental Tomes are removed from the experimental random-artifact pool until a six-school replacement set is authored; they should not silently map four old schools onto six new ones.":
        "A combat spell legitimately inscribed in a hero's spellbook may be cast regardless of School proficiency; School proficiency governs acquisition, not permission to use an already-inscribed spell. This grants no unknown or removed spell and bypasses no Spell Point cost, typed action, target, immunity, Spell Lock, or Adventure Spell rule. Spellbinder's Hat is redesigned around temporary availability: while equipped, all eligible Level 5 combat spells are inscribed in the hero's spellbook. Removing it ends only access supplied by the Hat and never erases independently learned spells. The four legacy elemental Tomes are removed from the experimental random-artifact pool until a six-school replacement set is authored; they should not silently map four old schools onto six new ones.",
    "Inferno heroes may bring actual reserve troops onto the battlefield through gates. Gating uses a Demonic Reserve of owned Inferno creatures outside the seven active combat slots; creatures are not created for free.":
        "Inferno heroes may bring actual reserve troops onto the battlefield through gates. Gating uses a Demonic Reserve of owned Inferno creatures outside the seven active combat slots; creatures are not created for free. The hero screen permits depositing and withdrawing the whole selected reserve stack subject to ordinary army legality. Casualties suffered by reserve troops are permanent unless a rule such as Endless Legion restores them.",
    "A friendly Inferno stack may spend its Creature Activation to open a Gate on a legal empty hex within 3 hexes. At the start of the next round, one Core stack from the Demonic Reserve arrives there.":
        "A friendly Inferno stack may spend its Creature Activation to open a Gate on a legal empty hex within 3 hexes. The server reserves the arriving stack's complete footprint, including both hexes for a double-wide creature; reserved hexes are impassable until arrival or cancellation. A flame-only marker shows the reserved footprint without a yellow selection outline. At the start of the next round, one Core stack from the Demonic Reserve arrives there, and the successful arrival plays the Devil movement flame sound.",
    "Surviving gated creatures return to the Demonic Reserve after combat unless another rule explicitly transfers them into the active army.":
        "Surviving gated creatures return to the Demonic Reserve after combat unless another rule explicitly transfers them into the active army. A failed or interrupted Gate request never removes its selected reserve stack; validation and reservation are authoritative and atomic.",
    "A gated stack gains temporary HP equal to 20% of its current aggregate HP until combat ends.":
        "A gated stack gains temporary HP equal to floor(20% of its current aggregate HP) until combat ends. This pool is consumed before creature HP, creates no creatures, cannot be healed or resurrected, and is serialized in battle saves.",
    "A stack may move up to half its Speed before using its activation to Gate.":
        "As one combined Creature Activation, a stack may optionally move along a legal path up to floor(half its Speed) and then Gate. The complete request is validated before commitment; choosing no movement is legal, and interrupted or rejected movement does not consume or remove the selected reserve stack.",
}


INSERT_AFTER = {
    "This makes Knowledge particularly transparent: without Intelligence, one point of effective Knowledge equals one point of normal Spell Point capacity.": [
        "Total Available Spell Points = Normal Spell Points + Buffer Spell Points. Normal Spell Points never exceed the current maximum; Buffer Spell Points may exceed it and are ordinary spendable Spell Points rather than a second currency.",
        "Spell costs consume Buffer first and then Normal. Granting Buffer never restores missing Normal Spell Points. Ordinary restoration fills Normal Spell Points only and neither consumes nor replaces Buffer unless an effect explicitly says otherwise.",
        "Capacity changes are event-driven. Increasing Knowledge or equipping a capacity artifact creates empty capacity rather than restoring points. Whenever an effect reducing maximum capacity is removed, excess Normal Spell Points are lost immediately; Buffer is unaffected. Re-equipping restores capacity, not the discarded points. Saves defensively normalize this state after the complete bonus tree is loaded.",
        "Magic Spring restores Normal Spell Points to maximum and grants +25 Buffer Spell Points. It never doubles the current pool or maximum. Town and Mage Guild rest, Magic Wells, immediate-refill buildings, regeneration, Mysticism-style effects, and ordinary restoration affect Normal only unless they explicitly grant Buffer.",
        "The familiar display shows total spendable points over normal maximum, followed by a yellow Buffer annotation already included in the total—for example, 310 / 460  +50 Buffer. Expanded help explains both pools and Buffer-first spending without disclosing hidden enemy capacity.",
    ],
    "External advancement therefore bypasses rank prerequisites, not perk order.": [
        "Every source that teaches or advances a Skill uses the live New Horizons Skill registry and current class, faction, slot, rank, and perk-order legality. When interaction is optional, ask whether the hero wishes to learn or advance the Skill; an already Expert hero receives an explanation instead of a redundant grant. Exceptional sources may advance rank as defined above, but never grant an arbitrary perk or erase a missing perk-tier choice. Building-specific replacements such as House of Wisdom scroll sales take precedence over generic teaching behavior.",
    ],
    "Normally, once per round, a hero uses their Hero Action to either cast one Spell or issue one Order. Specific abilities may explicitly permit an additional Spell or Order.": [
        "Different active Orders coexist through their own normal durations, targets, effects, and expiry. Issuing a new different Order never replaces an earlier Order. This rule grants no additional action and does not permit an otherwise forbidden repeat of the same Order; all prerequisites, use limits, trigger timing, and authoritative validation still apply.",
    ],
    "Magi and Arch Magi return to the stronger offensive role associated with Heroes II. They are intended to be premium Elite shooters: more dangerous and more expensive than Liches in focused ranged combat, while Liches retain their own area-pressure identity. Genies are correspondingly shifted downward in raw offense and toward mobility/support. These are prototype values pending the full creature-stat pass.": [
        "New Horizons renames Tower's Alchemist hero class to Battle Mage. Mage and Genie dwelling levels, building-card positions, costs, prerequisites, recruitment presentation, and upgrade dependencies are swapped as one coherent town change; both creature lines remain Elite. The Library stays associated with Mage and Arch Mage growth and follows the Mage dwelling's resulting position and prerequisites rather than the old Genie slot.",
        "New Horizons Solmyr remains a Wizard and begins with Metamagic, Havoc Magic, and Stormcaller. He begins with Master Chain Lightning instead of ordinary Chain Lightning and cannot learn the ordinary version. Master Chain Lightning retains ordinary Chain Lightning's Mana cost and first-target damage, loses less damage on later jumps, and improves jump retention as Solmyr gains levels. Legacy-mode Solmyr is unchanged. This is the first authored three-starting-development profile; do not invent third starting choices for other heroes before their profiles are authored.",
    ],
    "A combat spell legitimately inscribed in a hero's spellbook may be cast regardless of School proficiency; School proficiency governs acquisition, not permission to use an already-inscribed spell. This grants no unknown or removed spell and bypasses no Spell Point cost, typed action, target, immunity, Spell Lock, or Adventure Spell rule. Spellbinder's Hat is redesigned around temporary availability: while equipped, all eligible Level 5 combat spells are inscribed in the hero's spellbook. Removing it ends only access supplied by the Hat and never erases independently learned spells. The four legacy elemental Tomes are removed from the experimental random-artifact pool until a six-school replacement set is authored; they should not silently map four old schools onto six new ones.": [
        "Hero starting profiles audit starting Skills, spells, specialties, and armies together against the current registries and mechanics. Removed or replaced vanilla mechanics must not leave stale spells, empty starting choices, obsolete specialties, or armies that violate Leadership. Author a replacement where the design leaves a gap rather than inferring one; Halon's former Mysticism package and the remaining three-starting-development profiles therefore require explicit authored replacements before activation.",
    ],
    "The UI plan distinguishes systems that already have working presentation from mechanics that still require interaction, targeting, state feedback, or dedicated panels. The existing Hero screen redesign and the spellbook school bookmarks / school artwork are retained; they need integration with the rules below, not another visual redesign.": [
        "Battle logs record meaningful combat interactions with actor, cause, target, mechanical effect, and authoritative result. Metamagic follow-ups name the hero, follow-up ordinal, triggering Metamagic effect, spell, and resulting damage or other outcome. Orders and perks expose applicable numerical bonuses, prevention, formulas, and conditions, then record realized outcomes when they resolve. Use direct Heroes III-style wording, omit repetitive New Horizons prefixes and obsolete legacy labels, and attribute mitigation to the actual mechanic.",
        "All new purpose-made art, including provisional art, follows the Heroes III art workflow and retains source prompt/reference provenance. The UI/asset register covers every live Skill, perk, Order, spell, and derived attribute, classifying each asset as Not done, Provisional, or Final; borrowed unrelated icons are Not done, while Final requires explicit approval evidence. Historical school-bookmark and Metamagic classifications remain approval records rather than automatic claims about current runtime bindings.",
    ],
}


def paragraph_text(paragraph: etree._Element) -> str:
    return "".join(paragraph.xpath(".//w:t/text()", namespaces=NS)).strip()


def set_paragraph_text(paragraph: etree._Element, text: str) -> None:
    texts = paragraph.xpath(".//w:t", namespaces=NS)
    if not texts:
        run = etree.SubElement(paragraph, f"{{{W}}}r")
        node = etree.SubElement(run, f"{{{W}}}t")
        texts = [node]
    texts[0].text = text
    for node in texts[1:]:
        node.text = ""


def new_sibling_paragraph(source: etree._Element, text: str) -> etree._Element:
    paragraph = etree.Element(f"{{{W}}}p")
    props = source.find("w:pPr", namespaces=NS)
    if props is not None:
        paragraph.append(copy.deepcopy(props))
    run = etree.SubElement(paragraph, f"{{{W}}}r")
    source_run_props = source.find("w:r/w:rPr", namespaces=NS)
    if source_run_props is not None:
        run.append(copy.deepcopy(source_run_props))
    node = etree.SubElement(run, f"{{{W}}}t")
    node.text = text
    return paragraph


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="nh-canonical-docx-") as temp:
        unpacked = Path(temp) / "docx"
        with zipfile.ZipFile(DOCX) as archive:
            archive.extractall(unpacked)

        document_xml = unpacked / "word/document.xml"
        parser = etree.XMLParser(remove_blank_text=False)
        tree = etree.parse(str(document_xml), parser)
        paragraphs = tree.xpath(".//w:p", namespaces=NS)
        by_text: dict[str, list[etree._Element]] = {}
        for paragraph in paragraphs:
            by_text.setdefault(paragraph_text(paragraph), []).append(paragraph)

        changed = False
        for old, new in REPLACEMENTS.items():
            matches = by_text.get(old, [])
            if len(matches) == 1:
                set_paragraph_text(matches[0], new)
                by_text[old] = []
                by_text.setdefault(new, []).append(matches[0])
                changed = True
                continue
            if not matches and len(by_text.get(new, [])) == 1:
                continue
            if not matches and new in REPLACEMENTS:
                continue
            raise RuntimeError(f"expected one old or migrated paragraph for {old!r}, found {len(matches)}")

        paragraphs = tree.xpath(".//w:p", namespaces=NS)
        by_text = {paragraph_text(paragraph): paragraph for paragraph in paragraphs}
        for anchor, additions in INSERT_AFTER.items():
            source = by_text.get(anchor)
            if source is None:
                raise RuntimeError(f"missing insertion anchor: {anchor!r}")
            parent = source.getparent()
            position = parent.index(source) + 1
            for addition in additions:
                if addition in by_text:
                    continue
                parent.insert(position, new_sibling_paragraph(source, addition))
                position += 1
                changed = True

        if not changed:
            return

        tree.write(str(document_xml), xml_declaration=True, encoding="UTF-8", standalone="yes")

        candidate = Path(temp) / "New Horizons.docx"
        with zipfile.ZipFile(candidate, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(unpacked.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(unpacked).as_posix())
        with zipfile.ZipFile(candidate) as archive:
            bad = archive.testzip()
            if bad:
                raise RuntimeError(f"corrupt member after rewrite: {bad}")
        shutil.copy2(candidate, DOCX)


if __name__ == "__main__":
    main()
