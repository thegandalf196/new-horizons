#!/usr/bin/env python3
"""Source wiring guard, not visual acceptance, for compact hero Spell Points."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
widgets = (ROOT / "client/widgets/MiscWidgets.cpp").read_text()
callback = (ROOT / "lib/callback/CGameInfoCallback.cpp").read_text()
snapshot = (ROOT / "lib/gameState/InfoAboutArmy.cpp").read_text()
panel = widgets.split("class CompactHeroSpellPoints", 1)[1].split("InfoAboutHero accessibleHeroInfo", 1)[0]

assert "pos.w = 30" in panel and "pos.h = 20" in panel
assert "std::min(pos.w, originalSize.x)" in panel
assert "std::min(10, originalSize.y)" in panel
assert "10, Colors::YELLOW" in panel
assert "spellPointPresentation::tooltip(details.mana, details.manaLimit, details.bufferMana)" in panel
assert "CRClickPopup::createAndPush(explanation)" in panel
assert "details.manaLimit >= 0" in panel
assert widgets.count("std::make_shared<CompactHeroSpellPoints>") == 2
assert "CHeroTooltip(pos, accessibleHeroInfo(hero))" in widgets
assert "init(accessibleHeroInfo(hero))" in widgets
assert "if(hasAccess(h->tempOwner) && dest.details)\n\t\tdest.details->manaLimit = h->manaLimit();" in callback
assert "details->manaLimit = -1" in snapshot  # Visions does not gain capacity.
print("Compact hero Spell Points source guard PASS (visual QA still required)")
