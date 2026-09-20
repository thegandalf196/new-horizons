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
    '(spellProblem != ESpellCastProblem::OK && spellProblem != ESpellCastProblem::CASTS_PER_TURN_LIMIT)',
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
    assert 'const auto spellProblem = hero ? callback->battleCanCastSpell(hero, spells::Mode::HERO)' in method
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


def verify_orders_help(source):
    popup = source.split('ordersButton->addPopupCallback([this]', 1)[1].split('\n\t\t});', 1)[0]
    assert 'CPlayerInterface::battleInt.get() != &owner' in popup
    assert '!owner.curInt || !owner.actionsController' in popup
    assert 'CRClickPopup::createAndPush' in popup
    for forbidden in ('bOrdersf(', 'battleMakeSpellAction', 'block(false)', 'setShortcutBlocked'):
        assert forbidden not in popup
    assert 'CButton::tooltip("Orders", "")' in source
    assert re.search(r'ordersButton->block\(ordersBlocked\);\s*//[^\n]*\n\s*ordersButton->addUsedEvents\(SHOW_POPUP\);\s*setShortcutBlocked\(EShortcut::BATTLE_OPEN_ORDERS, ordersBlocked\);', source)


def verify_spent_action_spell_feedback(source):
    open_spellbook = source.split('void BattleWindow::openSpellbook()', 1)[1].split('void BattleWindow::bWaitf()', 1)[0]
    assert 'spellCastProblem == ESpellCastProblem::OK || spellCastProblem == ESpellCastProblem::CASTS_PER_TURN_LIMIT' in open_spellbook
    assert 'createAndPushWindow<CSpellWindow>' in open_spellbook
    assert 'SpellArea revalidates the selected spell' in open_spellbook
    assert 'CRClickPopup::createAndPush("The shared hero action has already been spent this round.' not in open_spellbook

    block_ui = source.split('void BattleWindow::blockUI(bool on)', 1)[1]
    assert 'spellcastingProblem == ESpellCastProblem::CASTS_PER_TURN_LIMIT' in block_ui
    assert 'on || tacticsMode || !canCastSpells' in block_ui

    hero = (SOURCE.parent / 'BattleHero.cpp').read_text()
    clicked = hero.split('void BattleHero::heroLeftClicked()', 1)[1].split('void BattleHero::heroRightClicked()', 1)[0]
    assert 'castProblem == ESpellCastProblem::OK || castProblem == ESpellCastProblem::CASTS_PER_TURN_LIMIT' in clicked

    spell_window = (SOURCE.parents[1] / 'windows/CSpellWindow.cpp').read_text()
    selection = spell_window.split('void CSpellWindow::SpellArea::clickPressed', 1)[1]
    assert 'mySpell->canBeCast(problem, battleCallback.get(), spells::Mode::HERO' in selection


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
    window = (SOURCE.parent / 'BattleWindow.cpp').read_text()
    verify_orders_help(window)
    verify_spent_action_spell_feedback(window)
    help_mutants = [
        window.replace('addUsedEvents(SHOW_POPUP)', 'addUsedEvents(SHOW_POPUP | LCLICK)', 1),
        window.replace('addUsedEvents(SHOW_POPUP)', 'addUsedEvents(SHOW_POPUP | KEYBOARD)', 1),
        window.replace('CRClickPopup::createAndPush(reason', 'bOrdersf(); CRClickPopup::createAndPush(reason', 1),
    ]
    for index, mutant in enumerate(help_mutants):
        assert mutant != window
        try:
            verify_orders_help(mutant)
        except AssertionError:
            continue
        raise AssertionError(f'Orders-help mutant {index} survived')
    print('PASS: read-only disabled Orders help; 3 click/key/dispatch mutants rejected')
    print('NOT compiled, actual-widget, lifetime, graphical or command-validation evidence')


if __name__ == '__main__':
    main()
