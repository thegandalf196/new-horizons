#!/usr/bin/env python3
"""Source routing regression, complements the native presentation-state test.
GPL-2.0-or-later. Does not compile, launch the game or simulate gameplay.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def body(source, signature):
    start = source.index('{', source.index(signature))
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

def check(source, window):
    for method in ('void CPlayerInterface::requestRealized(', 'void CPlayerInterface::queryResolved('):
        code = body(source, method)
        assert 'findWindows<HeroMasteryWindow>()' in code
        assert 'topWindow<HeroMasteryWindow>()' not in code
    resolved = body(source, 'void CPlayerInterface::queryResolved(')
    assert resolved.index('window->queryResolved(queryID)') < resolved.index('findPendingDialog(queryID)')
    pump = body(source, 'void CPlayerInterface::tryShowNextPendingDialog(')
    assert pump.index('isTopWindow(window.get())') < pump.index('window->close()') < pump.index('dialog.showCallback()')
    close = body(window, 'void HeroMasteryWindow::close(')
    assert 'state.canClose(ENGINE->windows().isTopWindow(this))' in close
    assert 'CWindowObject::close()' in close

source = (ROOT / 'client/CPlayerInterface.cpp').read_text()
window = (ROOT / 'client/windows/HeroMasteryWindow.cpp').read_text()
check(source, window)
# Reintroducing the reviewed top-only delivery or a blind non-top close must fail.
for broken_source, broken_window in (
    (source.replace('findWindows<HeroMasteryWindow>()', 'topWindow<HeroMasteryWindow>()'), window),
    (source, window.replace('state.canClose(ENGINE->windows().isTopWindow(this))', 'true')),
):
    try:
        check(broken_source, broken_window)
    except AssertionError:
        pass
    else:
        raise AssertionError('covered-dialog negative control accepted')
print('PASS actual source routing/top-only closure guards; two negative controls rejected. No GUI/native claim.')
