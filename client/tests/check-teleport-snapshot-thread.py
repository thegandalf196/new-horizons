#!/usr/bin/env python3
"""Focused wiring guard; run on the adopted tree with verified absolute Python."""
from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else Path(__file__).resolve().parents[2]


def read(name):
    return (root / name).read_text()


def body(source, signature):
    start = source.index('{', source.index(signature))
    depth = 1
    for end in range(start + 1, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError('Unclosed source function')


engine = read('client/GameEngine.cpp')
frame = body(engine, 'void GameEngine::updateFrame()')
assert frame.index('interfaceLock') < frame.index('guiTaskFailureShutdown.exchange(false)')
assert frame.index('engineUser->onShutdownRequested(false)') < frame.index('engineUser->onUpdate()')
assert frame.index('engineUser->onUpdate()') < frame.index('handleEvents()')
loop = body(engine, 'void GameEngine::mainLoop()')
assert '!guiTaskFailureShutdown.load() && !engineUser->wantsFrameRendered()' in loop
assert loop.index('input().fetchEvents()') < loop.index('wantsFrameRendered()')

controller = read('client/mapView/MapViewController.cpp')
before = body(controller, 'void MapViewController::onBeforeHeroTeleported')
assert 'captureTransitionSnapshotOnGui();' in before
assert 'view->createTransitionSnapshot' not in before
ending = body(controller, 'void MapViewController::endNetwork()')
assert ending.index('transitionSnapshotTask->cancel()') < ending.index('animationWait.requestTermination()')

capture = read('client/mapView/MapViewTransitionSnapshot.cpp')
assert '[view = view, context = context]' in capture and '[task]' in capture
assert '[this]' not in capture
assert capture.index('makeUnlockGuard(ENGINE->interfaceMutex)') < capture.index('task->wait()')
assert capture.index('task->wait()') < capture.index('task->wasCancelled()')
assert 'requestGuiTaskFailureShutdown()' in capture
assert 'throw GameShutdownException();' in capture
cache = body(read('client/mapView/MapViewCache.cpp'), 'void MapViewCache::createTransitionSnapshot')
assert cache.index('assert(ENGINE->amIGuiThread())') < cache.index('update(context)')

client = body(read('client/Client.cpp'), 'void CClient::handlePack')
assert client.index('pack.visit(beforeVisitor)') < client.index('gameState().apply(pack)')
for backend, event in (('clientsdl2', 'SDL_USEREVENT'), ('clientsdl3', 'SDL_EVENT_USER')):
    source = read(backend + '/events/InputHandler.cpp')
    post = body(source, 'bool InputHandler::tryDispatchMainThread')
    assert 'dispatchedTasks.tryPush' in post and 'SDL_PushEvent' in post
    preprocess = body(source, 'void InputHandler::preprocessEvent')
    user = preprocess[preprocess.index('ev.type == ' + event):]
    assert user.index('interfaceLock') < user.index('handleUserEvent')
    fetch = body(source, 'void InputHandler::fetchEvents()')
    assert 'preprocessEvent(ev)' in fetch
print('Teleport GUI snapshot ordering/shutdown wiring checks passed')
