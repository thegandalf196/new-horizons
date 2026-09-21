#!/usr/bin/env python3
"""Source/overlay controls ONLY. Does not compile, spawn, render, or test C++ behavior.

Without arguments: static policy and in-memory overlay rejection controls.
With --entry-overlay/--quick-overlay: require byte-exact private instrumentation.
Build must independently prove explicit EntryPoint.o replacement, old QuickPanel
archive member exclusion, object identities and unique symbols. Not proved here.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOCKED_END_NETWORK = ('std::scoped_lock interfaceLock(ENGINE->interfaceMutex);\n'
                      '\t\t\t\tGAME->server().endNetwork();')


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"nonunique/missing source seam: {old!r}")
    return text.replace(old, new, 1)


def expected_entry(source):
    source = replace_once(source, '#include "../client/GameEngine.h"',
                          '#include "../client/GameEngine.h"\n#include "QuickSpellWidgetDriver.h"')
    source = replace_once(source, 'const auto & runMainLoop = []()',
                          'std::unique_ptr<quickSpellWidgetTest::Driver> widgetDriver;\n\t\tconst auto & runMainLoop = [&widgetDriver]()')
    source = replace_once(source, '\t\t\t\t\tENGINE->mainLoop();',
                          '\t\t\t\t\twidgetDriver = std::make_unique<quickSpellWidgetTest::Driver>(*ENGINE, quickSpellWidgetTest::readOptionsFromEnvironment());\n\t\t\t\t\tENGINE->mainLoop();')
    source = replace_once(source, 'const auto & cleanupEngine = [&logConfigurator]()\n\t\t{',
                          'const auto & cleanupEngine = [&logConfigurator, &widgetDriver]()\n\t\t{\n\t\t\tif(widgetDriver) widgetDriver->stop();')
    return replace_once(source,
                        '\t\t\t\tstd::scoped_lock interfaceLock(ENGINE->interfaceMutex);\n\t\t\t\tGAME->server().endNetwork();',
                        '\t\t\t\tstd::scoped_lock interfaceLock(ENGINE->interfaceMutex);\n\t\t\t\tGAME->server().endNetwork();\n\t\t\t\tif(widgetDriver) widgetDriver->releaseGUIState();')


def expected_quick(source):
    source = replace_once(source, '#include "QuickSpellPanel.h"',
                          '#include "QuickSpellPanel.h"\n#include "QuickSpellWidgetProbe.h"')
    source = replace_once(source,
                          '\tif(!battle || (spell != SpellID::NONE && !newHorizonsMagic::spellAllowedByBattleRoster(*battle, spell)))\n\t\treturn;\n\tSettings configID = persistentStorage.write["quickSpell"][std::to_string(i)];',
                          '\tif(!battle || (spell != SpellID::NONE && !newHorizonsMagic::spellAllowedByBattleRoster(*battle, spell)))\n\t\treturn;\n\tquickSpellWidgetProbe::beforePreferenceWrite();\n\tSettings configID = persistentStorage.write["quickSpell"][std::to_string(i)];')
    return replace_once(source, '\t\t\t\tcurrent->castThisSpell(id);',
                        '\t\t\t{\n\t\t\t\tquickSpellWidgetProbe::beforeCastDispatch();\n\t\t\t\tcurrent->castThisSpell(id);\n\t\t\t}')


def startup_covered(entry):
    """Inert owner first; actual start INSIDE deferred main-loop invocation."""
    markers = ('std::unique_ptr<quickSpellWidgetTest::Driver> widgetDriver;',
               'const auto & runMainLoop = [&widgetDriver]()',
               'widgetDriver = std::make_unique<quickSpellWidgetTest::Driver>',
               'const auto & cleanupEngine = [&logConfigurator, &widgetDriver]()',
               'if(widgetDriver) widgetDriver->stop();',
               LOCKED_END_NETWORK,
               'if(widgetDriver) widgetDriver->releaseGUIState();',
               'auto onExit = vstd::makeScopeGuard(cleanupEngine);',
               '\n\t\trunMainLoop();')
    # Both original calls remain: ENGINE/locked branch and the no-ENGINE else.
    if entry.count('GAME->server().endNetwork();') != 2:
        return False
    if any(entry.count(marker) != 1 for marker in markers):
        return False
    positions = [entry.index(marker) for marker in markers]
    return positions == sorted(positions)


def policy(driver):
    required = (
        'result.operation == "BIND" || result.operation == "REPLACE" || result.operation == "VERIFY"',
        'post([weak, command]()',
        'engine.dispatchMainThread(task);',
        'if(!state || !state->active) return;',
        'command.sequence != sequence + 1',
        'pending.exchange(true)',
        'buffered.size() > 512',
        'O_RDWR | O_NONBLOCK | O_NOFOLLOW',
        'O_EXCL | O_NOFOLLOW | O_APPEND',
        'if(!active || !leaseValid() || !inspectionTime())',
        'std::this_thread::get_id() != guiThread',
        'engine->windows().topWindow<CSpellWindow>() != book',
        'window.updateQueue();',
        'currentCallback != callback || hero->id != heroID',
        'state->reader.request_stop();',
        'state->book.reset();',
        'state->callback.reset();',
        'state->battle.reset();',
    )
    if any(item not in driver for item in required):
        return False
    if any(item in driver for item in ('onBattleEnded(', 'onBattleStarted(',
                                       'interfaceMutex.lock(', 'lock_guard lock(engine.interfaceMutex',
                                       'dispatchMainThread([this', 'dispatchMainThread([state',
                                       'post([this', 'post([state',
                                       'persistentStorage.write', 'castThisSpell(')):
        return False
    stop = driver[driver.index('void Driver::stop() noexcept'):
                  driver.index('void Driver::releaseGUIState() noexcept')]
    if any(item in stop for item in ('state->book.reset();', 'state->callback.reset();',
                                    'state->battle.reset();', 'state.reset();')):
        return False
    release = driver[driver.index('void Driver::releaseGUIState() noexcept'):
                     driver.index('std::weak_ptr<void> Driver::nativeLifetimeObserver()')]
    if any(item not in release for item in ('state->book.reset();', 'state->callback.reset();',
                                           'state->battle.reset();', 'state.reset();',
                                           '|| !state->readerFinished || state->reader.joinable()')):
        return False
    revoke = driver[driver.index('void Driver::revoke() noexcept'):
                    driver.index('void Driver::stop() noexcept')]
    if any(item in revoke for item in ('join(', '.reset(', 'engine.')):
        return False
    for method in ('revoke', 'stop', 'releaseGUIState'):
        if f'void Driver::{method}() noexcept\n{{\n\tif(!state) return;' not in driver:
            return False
    ordered = ('state->active = false;', 'state->reader.request_stop();',
               'state->reader.join();')
    if any(item not in stop for item in ordered):
        return False
    positions = [stop.index(item) for item in ordered]
    return positions == sorted(positions)


def require_exact_overlay(actual, expected):
    if actual != expected:
        raise ValueError('overlay differs from exact allowed delta')


def native_mutants(driver):
    """Exact future C++ mutations. AUTHORING ONLY; no native discrimination claimed."""
    active = replace_once(driver, 'if(!state || !state->active) return;',
                          'if(!state) return;')
    active = replace_once(active, 'if(!active || !leaseValid() || !inspectionTime())',
                          'if(!leaseValid() || !inspectionTime())')
    lease = replace_once(driver, 'if(!active || !leaseValid() || !inspectionTime())',
                         'if(!active || !inspectionTime())')
    strong = replace_once(driver,
                          'const std::weak_ptr<State> weak = shared_from_this();\n\t\t\tpost([weak, command]()',
                          'post([strong = shared_from_this(), command]()')
    strong = replace_once(strong, 'const auto state = weak.lock();',
                          'const auto state = strong;')
    return {'active-both-guards': active, 'lease': lease, 'strong-capture': strong}


def native_policy(driver, test):
    required = ('GameEngine * const engine;', 'const bool nativeOnly;', 'const Dispatcher post;',
                'Driver::Driver(NativeDispatcherTag,',
                'std::weak_ptr<void> Driver::nativeLifetimeObserver() const',
                'if(!state->nativeOnly || state->engine',
                '|| state->book || state->callback || state->battle')
    if any(item not in driver for item in required):
        return False
    lease = driver.index('if(!active || !leaseValid() || !inspectionTime())')
    no_engine = driver.index('if(!engine) throw std::runtime_error("native-no-engine");')
    gui = driver.index('const auto current = CPlayerInterface::battleInt;')
    if not lease < no_engine < gui:
        return False
    release = driver[driver.index('void Driver::releaseNativeState() noexcept'):
                     driver.index('Driver::~Driver()')]
    if 'releaseGUIState(' in release or '.join(' in release:
        return False
    for marker in ('queue->awaitReceipt();', 'observer.expired()',
                   'const bool expiredBeforePump = observer.expired();',
                   'REJECT native-no-engine', 'REJECT lease-or-deadline',
                   'Driver::NativeDispatcherTag{}', 'std::chrono::seconds(3)', '::alarm(10)'):
        if marker not in test:
            return False
    # Static rejection only; Build still audits actual linked initializers/host access.
    return not any(marker in test for marker in ('new GameEngine', 'make_unique<GameEngine>',
                                                 'new GameInstance', 'SDL_Init(', 'SDL_CreateWindow('))


def controls(entry, quick, driver, test):
    """Static mutants, NOT execution of the reader/cancellation/native widgets."""
    assert policy(driver)
    assert native_policy(driver, test)
    authored_native_mutants = native_mutants(driver)
    assert len(authored_native_mutants) == 3
    assert all(body != driver for body in authored_native_mutants.values())
    assert startup_covered(entry)
    guard = 'auto onExit = vstd::makeScopeGuard(cleanupEngine);'
    call = '\n\t\trunMainLoop();'
    bad_start = replace_once(entry, guard, '/* guard moved too late */')
    bad_start = replace_once(bad_start, call, call + '\n\t\t' + guard)
    assert not startup_covered(bad_start)
    release_call = 'if(widgetDriver) widgetDriver->releaseGUIState();'
    bad_release = replace_once(entry, release_call, '/* release moved before NET join */')
    bad_release = replace_once(bad_release, LOCKED_END_NETWORK,
                               'std::scoped_lock interfaceLock(ENGINE->interfaceMutex);\n\t\t\t\t'
                               + release_call + '\n\t\t\t\tGAME->server().endNetwork();')
    assert not startup_covered(bad_release)
    early_reset = replace_once(driver, 'state->reader.request_stop();',
                               'state->book.reset();\n\tstate->reader.request_stop();')
    assert not policy(early_reset)
    non_idempotent = replace_once(driver, 'void Driver::stop() noexcept\n{\n\tif(!state) return;',
                                  'void Driver::stop() noexcept\n{\n\t/* missing null guard */')
    assert not policy(non_idempotent)
    unjoined_release = replace_once(driver,
                                    'if(state->nativeOnly || std::this_thread::get_id() != state->guiThread || state->active\n\t\t|| !state->readerFinished || state->reader.joinable())',
                                    'if(state->nativeOnly || std::this_thread::get_id() != state->guiThread || state->active\n\t\t|| !state->readerFinished)')
    assert not policy(unjoined_release)
    for seam in ('command.sequence != sequence + 1', 'pending.exchange(true)',
                 'state->book.reset();', 'if(!state || !state->active) return;'):
        assert not policy(replace_once(driver, seam, '/* REMOVED STATIC MUTANT */'))
    assert not policy(driver + '\nvoid forbidden() { onBattleEnded(); }\n')
    # Exact overlay equality rejects even innocuous extra or removed code.
    for expected in (entry, quick):
        require_exact_overlay(expected.encode(), expected.encode())
        for mutant in (expected + '\n// unauthorized delta\n', expected[:-1]):
            try:
                require_exact_overlay(mutant.encode(), expected.encode())
            except ValueError:
                pass
            else:
                raise AssertionError('overlay validator accepted a mutant')
    return {'scope': 'STATIC source policy + overlay equality controls ONLY',
            'source_mutants_rejected': 5, 'overlay_mutants_rejected': 4,
            'late_cleanup_guard_mutants_rejected': 1,
            'unsafe_cleanup_order_mutants_rejected': 2,
            'cleanup_idempotence_precondition_mutants_rejected': 2,
            'native_mutants_authored_not_executed': list(authored_native_mutants),
            'native_reader_cancellation': 'NOT RUN', 'widget_behavior': 'NOT RUN',
            'link_identity_and_archive_exclusion': 'REQUIRES Build admission'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--entry-overlay', type=Path)
    parser.add_argument('--quick-overlay', type=Path)
    args = parser.parse_args()
    entry = expected_entry((ROOT / 'clientapp/EntryPoint.cpp').read_text())
    quick = expected_quick((ROOT / 'client/battle/QuickSpellPanel.cpp').read_text())
    driver = (ROOT / 'client/tests/QuickSpellWidgetDriver.cpp').read_text()
    test = (ROOT / 'client/tests/QuickSpellWidgetCancellationTest.cpp').read_text()
    report = controls(entry, quick, driver, test)
    for name, path, expected in (('entry', args.entry_overlay, entry),
                                 ('quick', args.quick_overlay, quick)):
        if path is not None:
            require_exact_overlay(path.read_bytes(), expected.encode())
        report[name] = {'expected_sha256': hashlib.sha256(expected.encode()).hexdigest(),
                        'offered_overlay_checked': path is not None}
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
