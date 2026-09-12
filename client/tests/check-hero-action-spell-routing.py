#!/usr/bin/env python3
"""Source-only chooser activation contract, not a widget/runtime test.

Reads the owned C++ source; mutations remain in memory. No game/build/GUI launch.
"""
from pathlib import Path
import re

SOURCE = Path(__file__).resolve().parents[1] / 'battle/BattleHeroActionWindow.cpp'
GUARDS = (
    '!owner->makingTurn()',
    'owner->curInt->isAutoFightOn',
    'owner->isInTacticsMode()',
    'owner->actionsController->heroSpellcastingModeActive()',
    '!hero',
    'callback->battleCanCastSpell(hero, spells::Mode::HERO) != ESpellCastProblem::OK',
)


def verify(source):
    method = source.split('void BattleHeroActionWindow::chooseSpell()', 1)[1]
    method = method.split('void BattleHeroActionWindow::show(', 1)[0]
    method = re.sub(r'//[^\n]*|/\*.*?\*/', '', method, flags=re.S)
    # The unavailable-battle branch may close. A live battle must first pass a
    # fresh authority/input-state check, independently of a cached button block.
    assert 'if(!owner)' in method
    assert 'auto callback = owner->getBattle();' in method
    assert 'const auto * hero = owner->currentHero();' in method
    condition = 'if(' + ' || '.join(GUARDS) + ')'
    compact = lambda text: re.sub(r'\s+', '', text)
    denial = condition + '{ refresh(); return; }'
    body = compact(method)
    assert compact(denial) in body, 'Missing complete activation-time refusal'
    assert body.index(compact(denial)) < body.rindex('close();')
    # The approved path must remain reachable after the refusal branch.
    assert body.split(compact(denial), 1)[1] == 'close();owner->windowObject->openSpellbook();}'


def main():
    source = SOURCE.read_text()
    verify(source)
    method_start = source.index('void BattleHeroActionWindow::chooseSpell()')
    prefix, method = source[:method_start], source[method_start:]
    mutants = [method.replace(guard, 'false', 1) for guard in GUARDS]
    mutants += [
        method.replace('close();\n\towner->windowObject->openSpellbook();',
                       'return;\n\tclose();\n\towner->windowObject->openSpellbook();', 1),
        method.replace('refresh();\n\t\treturn;', 'refresh();', 1),
        method.replace('close();\n\towner->windowObject->openSpellbook();',
                       'owner->windowObject->openSpellbook();\n\tclose();', 1),
    ]
    for index, mutant in enumerate(mutants):
        assert mutant != method
        try:
            verify(prefix + mutant)
        except AssertionError:
            continue
        raise AssertionError(f'Source-contract mutant {index} survived')
    print(f'PASS: activation routing source contract; {len(mutants)} in-memory mutants rejected')
    print('NOT compiled, actual-widget, lifetime, graphical or command-validation evidence')


if __name__ == '__main__':
    main()
