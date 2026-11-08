#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Attunement presentation contract; not rendered or mechanic acceptance."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
presentation = (ROOT / 'client/battle/ElementalAttunementPresentation.h').read_text()
strip = (ROOT / 'client/battle/StackTemporaryStatusPresenter.cpp').read_text()
panel = (ROOT / 'client/battle/StackInfoBasicPanel.cpp').read_text()
hero = (ROOT / 'client/battle/PassiveFactionSkillPresentation.cpp').read_text()
status = (ROOT / 'client/battle/BattleHeroStatus.cpp').read_text()

# Derived recipient facts own eligibility and numbers; the client must not
# maintain a second creature list, activate flags or invent a spell effect.
assert 'unit.elementalAttunementPassives()' in presentation
assert '!unit.alive() || !facts.attuned' in presentation
assert 'facts.meleeDamagePercent' in presentation
assert 'facts.rangedDamagePercent' in presentation
assert 'Repeated elemental casts do not stack' in presentation
assert 'Primal Vigilance' in presentation and 'does not replenish' in presentation
assert 'StackStatusIconKind::ELEMENTAL_ATTUNEMENT' in strip
assert 'Rect(slotX, slotY, 48, 36)' in strip
assert 'attunement.attuned ? 1 : 0' in strip
assert 'displayedAttunement = elementalAttunementPresentation::forUnit(*stack)' in panel
assert 'elementalAttunementPresentation::forUnit(*updatedInfo) == displayedAttunement' in panel
assert 'getElementalAttunementMask(side)' in hero
assert 'battleGetFightingHero(side)' in status
assert 'passiveFactionSkillPresentation::elementalAttunement' in status
for forbidden in ('setElementalAttunementMask', 'activatedMask(', 'newHorizonsElementalRebirth::'):
    assert forbidden not in presentation + strip + hero + status
print('PASS: canonical Attunement readback, bounded status strip, refresh and visible-hero boundary')
