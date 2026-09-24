#!/usr/bin/env python3
"""Static guard for New Horizons creature-rank and Leadership Cost rows."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WINDOW = (ROOT / "client/windows/CCreatureWindow.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "client/windows/CCreatureWindow.h").read_text(encoding="utf-8")
ASSET = (ROOT / "client/render/AssetGenerator.cpp").read_text(encoding="utf-8")

assert "RANK" in HEADER
assert "std::array<std::shared_ptr<CIntObject>, 11> statIcons" in HEADER
assert "std::array<std::string, 11> statNames" in HEADER
assert "std::array<std::string, 11> statFormats" in HEADER
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
for nh, argument in (("", "false"), ("nh-", "true")):
    for suffix in range(3):
        assert f'"stackWindow/info-panel-{nh}rank-{suffix}.png"' in ASSET
        assert f"createCreatureInfoPanel({suffix + 2}, {argument}, true)" in ASSET
assert "(showNewHorizonsStats ? 10 : 9) + (showRank ? 1 : 0)" in ASSET

# Icons and the hover area use statRow so category-only legacy contexts retain
# their compact ninth-row layout before placing rank in the tenth slot.
assert "Rect(116, iconY[statRow(EStat::LEADERSHIP)], 20, 20)" in WINDOW
assert "Rect(116, iconY[statRow(EStat::RANK)], 20, 20)" in WINDOW
assert "Rect(114, iconY[statRow(EStat::RANK)], 194, 20)" in WINDOW

# Leadership continues to show the creature requirement and the owning hero's
# current/max stack count, if that context is available.
assert "leadershipRequirement = leadershipCapacity->requirement" in WINDOW
assert 'std::to_string(leadershipCount) + "/" + std::to_string(leadershipCapacity->maximum)' in WINDOW
assert "addStatLabel(EStat::LEADERSHIP, costText + capacityText)" in WINDOW

print("New Horizons creature rank and Leadership Cost UI: PASS")
