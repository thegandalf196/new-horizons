#!/usr/bin/env python3
"""Static input/range source checks; not rendered or playable acceptance."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPELL_WINDOW = (ROOT / "client/windows/CSpellWindow.cpp").read_text()
ADVENTURE_MAP = (ROOT / "client/adventureMap/AdventureMapInterface.cpp").read_text()
MAP_RENDERER = (ROOT / "client/mapView/MapRendererContext.cpp").read_text()


def between(source: str, start: str, end: str) -> str:
    left = source.index(start)
    right = source.index(end, left)
    return source[left:right]


# Adventure spells enter map selection only when the active effect requests it;
# legacy/non-targeted Adventure Spells retain their existing direct-cast path.
selection = between(SPELL_WINDOW, "else //adventure spell", "void CSpellWindow::SpellArea::showPopupWindow")
assert "getEffectAs<IAdventureSpellEffect>" in selection
assert "requiresTargetSelection(owner->myHero)" in selection
assert "adventureInt->enterCastingMode(mySpell)" in selection
assert "owner->myInt->cb->castSpell(h, mySpell->id)" in selection

# Invalid target clicks remain in casting mode; accepted clicks forward the
# actual selected tile through the existing callback command.
left_click = between(ADVENTURE_MAP, "void AdventureMapInterface::onTileLeftClicked", "void AdventureMapInterface::onTileHovered")
spell_click = between(left_click, "if(spellBeingCasted)", "if(getState() == EAdventureState::DISEMBARKING)")
assert "if(isValidAdventureSpellTarget(targetPosition))" in spell_click
assert "performSpellcasting(targetPosition)" in spell_click
assert spell_click.rstrip().endswith("return;\n\t}")

perform = between(ADVENTURE_MAP, "void AdventureMapInterface::performSpellcasting", "Rect AdventureMapInterface::terrainAreaPixels")
assert "exitCastingMode();" in perform
assert "cb->castSpell(GAME->interface()->localState->getCurrentHero(), id, dest);" in perform

# Keyboard/right-click cancellation shares one cleanup path and sends no cast.
key_cancel = between(ADVENTURE_MAP, "void AdventureMapInterface::keyPressed", "void AdventureMapInterface::onSelectionChanged")
right_cancel = between(ADVENTURE_MAP, "void AdventureMapInterface::onTileRightClicked", "void AdventureMapInterface::enterCastingMode")
abort = between(ADVENTURE_MAP, "void AdventureMapInterface::hotkeyAbortCastingMode", "void AdventureMapInterface::performSpellcasting")
assert "hotkeyAbortCastingMode()" in key_cancel
assert "hotkeyAbortCastingMode()" in right_cancel
assert "exitCastingMode();" in abort
assert "castSpell(" not in abort

# Hover and overlay use the shared effect API and are null-safe for non-ranged
# adventure effects such as Summon Boat.
hover = between(ADVENTURE_MAP, "void AdventureMapInterface::onTileHovered", "void AdventureMapInterface::onTileRightClicked")
assert "getEffectAs<IAdventureSpellEffect>(hero)" in hover
assert "spellEffect->isTargetInRange" in hover
assert "spellEffect->canBeCastAtImpl" in hover

range_overlay = between(MAP_RENDERER, "bool MapRendererAdventureContext::showSpellRange", "MapRendererAdventureTransitionContext::MapRendererAdventureTransitionContext")
assert "getEffectAs<IAdventureSpellEffect>(hero)" in range_overlay
assert "return spellEffect && !spellEffect->isTargetInRange" in range_overlay

print("PASS: Summon Boat targeting input/range source contract")
print("NOT rendered, GUI, or playable acceptance")
