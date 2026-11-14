#!/usr/bin/env python3
"""Static guard for New Horizons creature-rank and Leadership Cost rows."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WINDOW = (ROOT / "client/windows/CCreatureWindow.cpp").read_text(encoding="utf-8")
WINDOW += (ROOT / "client/windows/CreatureLeadershipPresentation.cpp").read_text(encoding="utf-8")
WINDOW += (ROOT / "client/windows/CreatureStatPresentation.cpp").read_text(encoding="utf-8")
LAYOUT = (ROOT / "client/windows/CreatureStatLayout.h").read_text(encoding="utf-8")
PANEL = (ROOT / "client/render/AssetGeneratorCreaturePanel.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "client/windows/CCreatureWindow.h").read_text(encoding="utf-8")
ASSET = (ROOT / "client/render/AssetGenerator.cpp").read_text(encoding="utf-8")

assert "registerCreaturePanelAssets();" in ASSET
assert "void AssetGenerator::registerCreaturePanelAssets()" in PANEL
assert "RANK" in LAYOUT
assert "std::array<std::shared_ptr<CIntObject>, creatureStatLayout::STAT_COUNT> statIcons" in HEADER
assert "std::array<std::string, creatureStatLayout::STAT_COUNT> statNames" in HEADER
assert "std::array<std::string, creatureStatLayout::STAT_COUNT> statFormats" in HEADER
assert '"Rank"' in WINDOW
assert 'AnimationPath::builtin("NH_creature_rank_20")' in WINDOW
assert 'AnimationPath::builtin("NH_creature_leadership_20")' in WINDOW

# The rank is optional, sourced from the saved callback view, and lives in the
# ordinary horizontal stat table with its translated category description.
assert "const bool showRank = info->category.has_value();" in WINDOW
assert "addStatLabel(EStat::RANK, nameText)" in WINDOW
assert '"Rank: " + nameText' in WINDOW
assert '"{" + nameText + "}\\n\\n" + description' in WINDOW
assert "getBackgroundName(showExp, showArt, showNewHorizonsStats, showRank)" in WINDOW
assert 'prefix + (showRank ? "rank-" : "")' in WINDOW
assert "categorySection" not in WINDOW

# Recruitment previews retain the originating saved classification without
# borrowing bonuses from the destination army or consulting a global profile.
RECRUITMENT = (ROOT / "client/windows/CRecruitmentWindow.cpp").read_text(encoding="utf-8")
assert "parent->dst ? parent->dst->cb : (parent->dwelling ? parent->dwelling->cb : nullptr)" in RECRUITMENT
assert "createAndPushWindow<CStackWindow>(creature, true, context)" in RECRUITMENT
assert "info->categoryContext = categoryContext;" in WINDOW
assert "else if(info->categoryContext)" in WINDOW
assert "info->categoryContext->getCreatureCategory(info->creature->getId())" in WINDOW

for nh, argument in (("", "false"), ("nh-", "true")):
    for suffix in range(3):
        assert f'"stackWindow/info-panel-{nh}rank-{suffix}.png"' in PANEL
        assert f"createCreatureInfoPanel({suffix + 2}, {argument}, true)" in PANEL
assert "(newHorizons ? 13 : 9) + (rank ? 1 : 0)" in LAYOUT
assert "creatureStatLayout::panelRows(showNewHorizonsStats, showRank)" in PANEL

# Icons and the hover area use statRow so category-only legacy contexts retain
# their compact ninth-row layout before placing rank in the tenth slot.
assert "Rect(116, creatureStatLayout::iconY(EStat::LEADERSHIP, showNewHorizonsStats), 20, 20)" in WINDOW
assert "Rect(116, creatureStatLayout::iconY(EStat::RANK, showNewHorizonsStats), 20, 20)" in WINDOW
assert "Rect(114, creatureStatLayout::iconY(EStat::RANK, showNewHorizonsStats), 194, 20)" in WINDOW

# Leadership continues to show the creature requirement and the owning hero's
# current/max stack count, if that context is available.
assert "battle->battleLeadershipReadout(stack)" in WINDOW
assert "newHorizonsHeroes::leadershipStackReadout(hero, creature, count, nullptr, hero)" in WINDOW
assert '"%REQUIREMENT%", std::to_string(readout.requirement)' in WINDOW
assert '"%COUNT%", std::to_string(readout.count)' in WINDOW
assert '"%CAPACITY%", std::to_string(readout.capacity)' in WINDOW
assert "addStatLabel(EStat::LEADERSHIP, readout.active ? value" in WINDOW
assert "leadershipPresentation::detailText(readout)" in WINDOW

print("New Horizons creature rank and Leadership Cost UI: PASS")
