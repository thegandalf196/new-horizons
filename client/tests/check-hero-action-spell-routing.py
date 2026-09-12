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


def verify_redraw(source):
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', source, flags=re.S)
    for name in ('show', 'showAll'):
        expected = (
            rf'void BattleHeroActionWindow::{name}\(Canvas & canvas\)\s*'
            rf'\{{\s*refresh\(\);\s*CWindowObject::{name}\(canvas\);\s*\}}'
        )
        assert re.search(expected, source), f'{name} must refresh before its base render'


def main():
    source = SOURCE.read_text()
    verify(source)
    verify_redraw(source)
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
    redraw_mutants = []
    for name in ('show', 'showAll'):
        body = f'refresh();\n\tCWindowObject::{name}(canvas);'
        redraw_mutants += [
            source.replace(body, f'CWindowObject::{name}(canvas);', 1),
            source.replace(body, f'CWindowObject::{name}(canvas);\n\trefresh();', 1),
        ]
    for index, mutant in enumerate(redraw_mutants):
        assert mutant != source
        try:
            verify_redraw(mutant)
        except AssertionError:
            continue
        raise AssertionError(f'Redraw-contract mutant {index} survived')
    print(f'PASS: both redraw paths; {len(redraw_mutants)} missing/late-refresh mutants rejected')
    print('NOT compiled, actual-widget, lifetime, graphical or command-validation evidence')


if __name__ == '__main__':
    main()
