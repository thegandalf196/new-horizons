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
    compact_block_ui = re.sub(r'\s+', '', block_ui)
    assert 'constbooldeploymentPhase=owner.isDeploymentPhase();' in compact_block_ui
    assert 'setShortcutBlocked(EShortcut::BATTLE_CAST_SPELL,on||deploymentPhase||!canCastSpells);' in compact_block_ui

    hero = (SOURCE.parent / 'BattleHero.cpp').read_text()
    clicked = hero.split('void BattleHero::heroLeftClicked()', 1)[1].split('void BattleHero::heroRightClicked()', 1)[0]
    assert 'castProblem == ESpellCastProblem::OK || castProblem == ESpellCastProblem::CASTS_PER_TURN_LIMIT' in clicked

    spell_window = (SOURCE.parents[1] / 'windows/CSpellWindow.cpp').read_text()
    selection = spell_window.split('void CSpellWindow::SpellArea::clickPressed', 1)[1]
    assert 'mySpell->canBeCast(problem, battleCallback.get(), spells::Mode::HERO' in selection
    assert 'if(schoolLocked)' in selection
    assert 'showInfoDialog(schoolRequirementText)' in selection
    assert 'const bool ownedOrGranted = !myHero->getSourcesForSpell' in spell_window
    verify_spellbook_access(*spellbook_access_sources())


def spellbook_access_sources():
    root = SOURCE.parents[2]
    return tuple((root / path).read_text() for path in (
        'client/windows/CSpellWindow.cpp',
        'client/windows/SpellbookEntryPresentation.cpp',
        'lib/mapObjects/HeroSpellCasting.cpp',
        'lib/mapObjects/CGHeroInstance.cpp',
    ))


def verify_spellbook_access(window, presentation, casting, hero):
    clean = lambda text: re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)
    compact = lambda text: re.sub(r'\s+', '', clean(text))
    access = presentation.split('Access access(', 1)[1].split('std::string levelAndSchools(', 1)[0]
    access = compact(access[access.index('{'):])
    expected = (
        '{ const int required = newHorizonsMagic::requiredSchoolRank(hero.getMagicRules(), spell.getId());'
        ' const bool inscribed = hero.hasSpellbook() && hero.isSpellInscribedForCasting(spell.getId());'
        ' Access result; result.schoolLocked = required > 0 && !inscribed'
        ' && !newHorizonsMagic::hasSchoolProficiency(&hero, spell.getId()) && !hero.canCastThisSpell(&spell);'
        ' if(!result.schoolLocked) return result;'
    )
    assert access.startswith(compact(expected)), 'Saved-rules proficiency/inscription/casting access predicate changed'
    for token in (
        'newHorizonsMagic::spellSchoolSkills(hero.getMagicRules(), spell.getId())',
        'MetaString requirement = MetaString::createFromTextID("new-horizons.spellbook.requirement");',
        'TextIdentifier("core.skilllev", required - 1).get()',
        'requirement.replaceRawString(schools);',
        'result.requirement = requirement.toString(&translator);',
    ):
        assert compact(token) in access, 'Missing rank/school feedback binding: ' + token
    refresh = window.split('SpellCostBreakdown CSpellWindow::SpellArea::refreshReadout()', 1)[1]
    assert compact(refresh).startswith(compact(
        '{ const auto access = spellbookPresentation::access(*owner->myHero, *mySpell, GAME->translator());'
        ' schoolLocked = access.schoolLocked; schoolRequirementText = access.requirement;'
    )), 'Spell slot must consume this hero/spell access result, not an unused helper call'
    setter = window.split('void CSpellWindow::SpellArea::setSpell(', 1)[1].split('SpellCostBreakdown CSpellWindow::SpellArea::refreshReadout()', 1)[0]
    assert 'schoolLocked=false;' in compact(setter) and 'schoolRequirementText.clear();' in compact(setter)
    assert compact(setter).endswith('refreshReadout();}}'), 'Assigned spell must refresh access after slot reset'
    selected = window.split('void CSpellWindow::SpellArea::clickPressed', 1)[1].split('void CSpellWindow::SpellArea::showPopupWindow', 1)[0]
    denial = compact('if(schoolLocked) { GAME->interface()->showInfoDialog(schoolRequirementText); return; }')
    assert denial in compact(selected)
    assert compact(selected).index(denial) < compact(selected).index('if(owner->onSpellSelect)')
    combat = selected.split('else if(combatSpell)', 1)[1].split('else //adventure spell', 1)[0]
    assert compact('const bool canCast = mySpell->canBeCast(problem, battleCallback.get(), spells::Mode::HERO, owner->myHero); if(canCast) {') in compact(combat)
    feedback = 'problem.getAll(texts); if(!texts.empty()) GAME->interface()->showInfoDialog(texts.front());'
    assert compact(feedback) in compact(combat), 'Spent action must surface the authoritative cast problem'
    # The canonical Hero path restricts the bypass to inscriptions or an actual
    # equipped, map-allowed matching Scroll; arbitrary source bonuses do not suffice.
    cast = casting.split('bool CGHeroInstance::canCastThisSpell(', 1)[1]
    assert compact('if(!inSpellBook && !newHorizonsMagic::hasSchoolProficiency(this, spell->getId()) && !equippedScrollGrantsCasting(*this, *spell, cb->isAllowed(spell->getId()))) return false;') in compact(cast)
    scroll = casting.split('bool equippedScrollGrantsCasting(', 1)[1].split('bool CGHeroInstance::canCastThisSpell(', 1)[0]
    for token in ('if(!spell.isCombat() || spell.isSpecial() || !mapAllowed) return false;',
                  'hero.artifactsWorn', 'hero.getArt(position)',
                  'if(!artifact || !artifact->isScroll() || artifact->getScrollSpellID() != spell.getId()) continue;',
                  'if(vstd::contains(sources, BonusSourceID(artifact->getId()))) return true;'):
        assert compact(token) in compact(scroll), 'Missing genuine Scroll access boundary: ' + token
    inscription = hero.split('bool CGHeroInstance::isSpellInscribedForCasting(', 1)[1].split('bool CGHeroInstance::isSpellbinderHatGrantEligible(', 1)[0]
    assert compact('return spellbookContainsSpell(spellId) || isSpellbinderHatGrantEligible(spellId);') in compact(inscription)


def verify_spellbook_access_mutants():
    sources = spellbook_access_sources()
    # Each mutation is applied to its precise owner/caller, never to a comment or
    # a different method that merely contains the same API name.
    changes = (
        (1, 'newHorizonsMagic::requiredSchoolRank(hero.getMagicRules(), spell.getId())', '0'),
        (1, 'hero.hasSpellbook() && hero.isSpellInscribedForCasting(spell.getId())', 'hero.isSpellInscribedForCasting(spell.getId())'),
        (1, 'hero.isSpellInscribedForCasting(spell.getId())', 'hero.spellbookContainsSpell(spell.getId())'),
        (1, 'required > 0 && !inscribed', 'required > 0'),
        (1, 'required > 0 && !inscribed', 'required > 0 && inscribed'),
        (1, '!newHorizonsMagic::hasSchoolProficiency(&hero, spell.getId())', 'true'),
        (1, '!hero.canCastThisSpell(&spell)', 'true'),
        (1, '!hero.canCastThisSpell(&spell)', 'hero.canCastThisSpell(&spell)'),
        (1, 'required - 1', '0'),
        (1, 'requirement.replaceRawString(schools);', ''),
        (0, 'spellbookPresentation::access(*owner->myHero, *mySpell, GAME->translator())', 'spellbookPresentation::access(*owner->myHero, *spell, GAME->translator())'),
        (0, 'schoolLocked = access.schoolLocked;', 'schoolLocked = false;'),
        (0, 'schoolRequirementText = access.requirement;', 'schoolRequirementText.clear();'),
        (0, '\t\trefreshReadout();\n\t}\n}\n\nSpellCostBreakdown', '\t}\n}\n\nSpellCostBreakdown'),
        (0, 'showInfoDialog(schoolRequirementText);', 'showInfoDialog("");'),
        (0, 'mySpell->canBeCast(problem, battleCallback.get(), spells::Mode::HERO,', 'true; /* mySpell->canBeCast(problem, battleCallback.get(), spells::Mode::HERO, */'),
        (0, 'problem.getAll(texts);', '/* no authoritative problem */'),
        (2, '!equippedScrollGrantsCasting(*this, *spell, cb->isAllowed(spell->getId()))', 'true'),
        (2, '!spell.isCombat() || spell.isSpecial() || !mapAllowed', '!spell.isCombat() || spell.isSpecial()'),
        (2, 'hero.artifactsWorn', 'hero.artifactsInBackpack'),
        (2, '!artifact->isScroll() || ', ''),
        (2, 'artifact->getScrollSpellID() != spell.getId()', 'false'),
        (2, 'vstd::contains(sources, BonusSourceID(artifact->getId()))', 'true'),
        (3, 'spellbookContainsSpell(spellId) || isSpellbinderHatGrantEligible(spellId)', 'spellbookContainsSpell(spellId)'),
    )
    for index, (owner, original, replacement) in enumerate(changes):
        mutant = list(sources)
        assert original in mutant[owner], f'Spellbook access mutant {index} is stale'
        mutant[owner] = mutant[owner].replace(original, replacement, 1)
        try:
            verify_spellbook_access(*mutant)
        except AssertionError:
            continue
        raise AssertionError(f'Spellbook access owner/caller mutant {index} survived')
    print(f'PASS: spellbook access owner/caller contract; {len(changes)} in-memory mutants rejected')


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
    verify_spellbook_access_mutants()
    deployment_mutants = [
        window.replace('on || deploymentPhase || !canCastSpells',
                       'on || localTacticsMode || !canCastSpells', 1),
        window.replace('on || deploymentPhase || !canCastSpells',
                       'on || !canCastSpells', 1),
        window.replace('const bool deploymentPhase = owner.isDeploymentPhase();',
                       'const bool deploymentPhase = owner.isInTacticsMode();', 1),
    ]
    for index, mutant in enumerate(deployment_mutants):
        assert mutant != window
        try:
            verify_spent_action_spell_feedback(mutant)
        except AssertionError:
            continue
        raise AssertionError(f'Global deployment spell-block mutant {index} survived')
    print(f'PASS: global deployment blocks spell routing; {len(deployment_mutants)} local/missing-phase mutants rejected')
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
