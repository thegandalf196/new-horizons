#!/usr/bin/env python3
"""Source-only null-safe caster routing; no native/widget/lifetime proof."""
from pathlib import Path
import re


def verify(source):
    body = source.split('void BattleActionsController::castThisSpell(SpellID spellID)', 1)[1]
    body = body.split('const CSpell * BattleActionsController::getHeroSpellToCast', 1)[0]
    body = re.sub(r'//[^\n]*|/\*.*?\*/', '', body, flags=re.S)
    body = re.sub(r'\s+', '', body)
    prefix = '{if(!owner.curInt)return;constauto*castingHero=owner.currentHero();if(!castingHero)return;'
    assert body.startswith(prefix), 'Caster refusal must precede targeting-state mutation'
    assert 'attackingHeroInstance->' not in body
    assert body.index('heroSpellToCast=') > len(prefix) - 1
    # Refusal guards must not accidentally disable the normal targeting route.
    assert 'return;' not in body[len(prefix):]
    assert 'getCasterAction(spellID.toSpell(),castingHero,spells::Mode::HERO)' in body


def verify_quick_slot(source):
    body = source.split('void BattleWindow::useSpellIfPossible(int slot)', 1)[1].split('void BattleWindow::toggleQueueVisibility()', 1)[0]
    body = re.sub(r'//[^\n]*|/\*.*?\*/', '', body, flags=re.S)
    body = re.sub(r'\s+', '', body)
    guard = ('if(CPlayerInterface::battleInt.get()!=&owner||!owner.curInt||owner.curInt->isAutoFightOn'
             '||owner.isInTacticsMode()||!owner.makingTurn()||!quickSpellWindow)return;')
    assert body.startswith('{' + guard)
    bounds = 'if(slot<0||static_cast<size_t>(slot)>=quickSpells.size())return;'
    assert bounds in body
    assert body.index(bounds) < body.index('quickSpells[slot]')
    tail = body.split('constautoid=std::get<0>(quickSpells[slot]);', 1)[1]
    assert tail == ('constauto*hero=owner.currentHero();'
                    'if(id.hasValue()&&hero&&id.toSpell()->canBeCast(owner.getBattle().get(),spells::Mode::HERO,hero))'
                    '{owner.castThisSpell(id);}};')


def main():
    path = Path(__file__).resolve().parents[1] / 'battle/BattleActionsController.cpp'
    source = path.read_text()
    verify(source)
    start = source.index('void BattleActionsController::castThisSpell(SpellID spellID)')
    prefix, method = source[:start], source[start:]
    mutants = [
        method.replace('\n\theroSpellToCast =', '\n\treturn;\n\theroSpellToCast =', 1),
        method.replace('if(!owner.curInt)\n\t\treturn;', '', 1),
        method.replace('if(!castingHero)\n\t\treturn;', '', 1),
        method.replace('owner.currentHero()', '(owner.attackingHeroInstance->tempOwner == owner.curInt->playerID) ? owner.attackingHeroInstance : owner.defendingHeroInstance', 1),
        method.replace('\n\tif(!owner.curInt)', '\n\theroSpellToCast = std::make_shared<BattleAction>();\n\tif(!owner.curInt)', 1),
    ]
    for index, mutant in enumerate(mutants):
        assert mutant != method
        try:
            verify(prefix + mutant)
        except AssertionError:
            continue
        raise AssertionError(f'Source-contract mutant {index} survived')
    print(f'PASS: null-safe caster source routing; {len(mutants)} in-memory mutants rejected')
    window = (path.parent / 'BattleWindow.cpp').read_text()
    verify_quick_slot(window)
    start = window.index('void BattleWindow::useSpellIfPossible(int slot)')
    prefix, method = window[:start], window[start:]
    guards = ('CPlayerInterface::battleInt.get() != &owner', '!owner.curInt',
              'owner.curInt->isAutoFightOn', 'owner.isInTacticsMode()',
              '!owner.makingTurn()', '!quickSpellWindow', 'slot < 0',
              'static_cast<size_t>(slot) >= quickSpells.size()')
    for guard in guards:
        mutant = method.replace(guard, 'false', 1)
        assert mutant != method
        try:
            verify_quick_slot(prefix + mutant)
        except AssertionError:
            continue
        raise AssertionError(f'Quick-slot guard mutant survived: {guard}')
    print('PASS: quick-slot activation and bounds; 8 guard mutants rejected')
    print('NOT compiled, actual-widget, lifetime or graphical evidence')


if __name__ == '__main__':
    main()
