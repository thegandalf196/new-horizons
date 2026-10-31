#!/usr/bin/env python3
"""Source regression for replacing a scenario while combat UI is active."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]


def body(source: str, signature: str) -> str:
    match = re.search(re.escape(signature) + r"\s*\{", source)
    if not match:
        raise AssertionError(f"missing function: {signature}")
    start = match.end()
    depth = 1
    index = start
    while depth and index < len(source):
        depth += (source[index] == "{") - (source[index] == "}")
        index += 1
    if depth:
        raise AssertionError(f"unterminated function: {signature}")
    return source[start:index - 1]


client = (ROOT / "client/Client.cpp").read_text(encoding="utf-8")
server = (ROOT / "client/CServerHandler.cpp").read_text(encoding="utf-8")
windows = (ROOT / "client/gui/WindowHandler.cpp").read_text(encoding="utf-8")
entry = (ROOT / "clientapp/EntryPoint.cpp").read_text(encoding="utf-8")


def check_quit_mutex_ownership(source: str) -> None:
    cleanup = body(source[source.index("const auto & cleanupEngine"):], "()")
    scope_start = cleanup.index("{")
    locked = body(cleanup, "")
    after_lock = cleanup[scope_start + len(locked) + 2:]
    assert 'if(settings["session"]["headless"].Bool() && GAME->server().client)' in cleanup[:scope_start], \
        "preserve the independent headless teardown before GUI locking"
    assert "GAME->server().endGameplay();" in cleanup[:scope_start], \
        "headless shutdown must still end gameplay without an engine"
    acquisition = ("if(ENGINE)\n"
                   "\t\t\t\t\tinterfaceLock = std::unique_lock<std::mutex>(ENGINE->interfaceMutex);")
    assert "std::unique_lock<std::mutex> interfaceLock;" in locked and acquisition in locked, \
        "GUI cleanup must own the mutex conditionally; ENGINE-null shutdown must not dereference it"
    assert "interfaceLock.unlock()" not in locked and "interfaceLock.release()" not in locked, \
        "do not release cleanup ownership before gameplay wait guards finish"
    network = "GAME->server().endNetwork();"
    gameplay = "GAME->server().endGameplay();"
    clear_windows = "ENGINE->windows().clear();"
    assert locked.count(network) == 2, "preserve both ENGINE and ENGINE-null network shutdown paths"
    assert locked.count(gameplay) == 1 and locked.count(clear_windows) == 1, \
        "gameplay and window teardown must remain inside the same owned lock scope"
    assert locked.rfind(network) < locked.index(gameplay) < locked.index(clear_windows), \
        "stop network waiters before gameplay and window destruction"
    assert 'if(!settings["session"]["headless"].Bool())' in locked, \
        "keep non-headless gameplay teardown conditional for menu/no-client shutdown"
    assert "if(GAME->server().client)" in locked, "menu shutdown must not require a gameplay client"
    assert locked.index("cureIntegration->detach();") < locked.find(network), \
        "integration detach must keep its existing ordering before network shutdown"
    assert all(step not in locked for step in ("GAME.reset();", "ENGINE->async().wait();", "ENGINE.reset();")), \
        "release the interface mutex before owner destruction and asynchronous cleanup"
    assert after_lock.index("GAME.reset();") < after_lock.index("ENGINE->async().wait();") < after_lock.index("ENGINE.reset();"), \
        "preserve game destruction, asynchronous drain and engine destruction ordering"


check_quit_mutex_ownership(entry)
# Wiring witnesses only: they do not replace a real new-game/load/normal-Quit run.
for broken in (
    entry.replace("std::unique_lock<std::mutex> interfaceLock;", ""),
    entry.replace("GAME->server().endGameplay();", ""),
    entry.replace("ENGINE->windows().clear();", "interfaceLock.unlock();\nENGINE->windows().clear();"),
    entry.replace("if(GAME->server().client)", "if(true)"),
    entry.replace("GAME.reset();", ""),
):
    try:
        check_quit_mutex_ownership(broken)
    except (AssertionError, ValueError):
        pass
    else:
        raise AssertionError("Quit source guard accepted missing ownership or teardown ordering")

restart = body(server, "void CServerHandler::restartGameplay()")
restart_steps = [
    "client->endNetwork();",
    "client->finishGameplay();",
    "client->endGame();",
    "client.reset();",
]
positions = [restart.find(step) for step in restart_steps]
assert all(position >= 0 for position in positions), restart_steps
assert positions == sorted(positions), "restart must terminate waiters before destroying client state"


def check_restart_text_lifecycle(restart_body: str) -> None:
    teardown = restart_body.find("client.reset();")
    release = restart_body.find("gameplayTextOverlays.clear();")
    lobby = restart_body.find("logicConnection->enterLobbyConnectionMode();")
    assert teardown >= 0 and teardown < release < lobby, \
        "old map texts must remain through client/UI teardown, then leave before a new session"


check_restart_text_lifecycle(restart)
for broken in (
    restart.replace("gameplayTextOverlays.clear();", ""),
    restart.replace("gameplayTextOverlays.clear();", "").replace(
        "client->finishGameplay();", "gameplayTextOverlays.clear();\nclient->finishGameplay();"),
):
    try:
        check_restart_text_lifecycle(broken)
    except AssertionError:
        pass
    else:
        raise AssertionError("restart text lifecycle accepted missing or premature overlay release")

starting = body(server, "void CServerHandler::startGameplay(std::shared_ptr<CGameState> gameState)")
assert "gameplayTextOverlays.emplace_back(gameState->getMap().texts);" in starting, \
    "the new session must install its own map texts"
assert "gameplayTextOverlays.emplace_back(si->campState->getTexts());" in starting, \
    "campaign-wide text sources must survive via the new session"
assert re.search(r"for\(const auto & scenarioTexts : si->campState->getScenarioTexts\(\)\)\s*"
                 r"gameplayTextOverlays.emplace_back\(scenarioTexts.second\);", starting), \
    "transferred heroes must retain current-campaign prior-scenario text sources"

ending = body(client, "void CClient::endGame()")
detach = "auto endingBattleInterface = std::move(CPlayerInterface::battleInt);"
terminate = "endingBattleInterface->endNetwork();"
clear_gui = "removeGUI();"
destroy = "endingBattleInterface.reset();"
verify = "assert(!CPlayerInterface::battleInt);"
positions = [ending.find(step) for step in (detach, terminate, clear_gui, destroy, verify)]
assert all(position >= 0 for position in positions), "missing deterministic battle teardown step"
assert positions == sorted(positions), "battleInt must clear before window removal and destruction"

remove_gui = body(client, "void CClient::removeGUI() const")
assert "ENGINE->windows().clear();" in remove_gui, "endGame must remove the BattleWindow"

clear = body(windows, "void WindowHandler::clear()")
assert "windowsStack.clear();" in clear, "window teardown must empty the window stack"
assert "redrawState.clearScene();" in clear, "window teardown must invalidate prior-scene deferred paints"


def check_scene_layer_release(clear_body: str) -> None:
    loop = "for(size_t layer = 0; layer < static_cast<size_t>(GpuRenderLayer::COUNT); ++layer)"
    release = "ENGINE->screenHandler().releaseLayer(static_cast<GpuRenderLayer>(layer));"
    assert loop in clear_body, "scene teardown must cover every GPU layer, including inactive windows"
    assert re.search(re.escape(loop) + r"\s*" + re.escape(release), clear_body), \
        "scene teardown must request deferred release for each layer"
    assert clear_body.find("disposed.clear();") < clear_body.find(loop) < clear_body.find("redrawState.clearScene();"), \
        "release scene layers after removing windows and before requesting the new full repaint"


check_scene_layer_release(clear)
# In-memory negative source witnesses; these are wiring checks, not GPU rendering.
for broken in (clear.replace("GpuRenderLayer::COUNT", "GpuRenderLayer::BATTLE"),
               clear.replace("ENGINE->screenHandler().releaseLayer(static_cast<GpuRenderLayer>(layer));", "")):
    try:
        check_scene_layer_release(broken)
    except AssertionError:
        pass
    else:
        raise AssertionError("scene-layer source guard accepted a missing or incomplete release")


# A rejected/late activation may find no battlefield in release builds. Once
# its existing defensive command has been sent, it must not touch that window.
player = (ROOT / "client/CPlayerInterface.cpp").read_text(encoding="utf-8")


def check_missing_battle_interface_return(source: str) -> None:
    activation = body(source, "void CPlayerInterface::activeStack(const BattleID & battleID, const CStack * stack) //called when it's turn of that stack")
    fallback = body(activation, "if(!battleInt)")
    defend = "cb->battleMakeUnitAction(battleID, BattleAction::makeDefend(stack));"
    assert fallback.count(defend) == 1, "preserve exactly one existing fallback command"
    assert re.search(re.escape(defend) + r"\s*return\s*;\s*$", fallback), \
        "a handled missing battlefield must return before later UI access"
    assert "battleInt->stackActivated(stack);" in activation, \
        "ordinary human activation must still reach its battlefield"
    assert activation.index("if(!battleInt)") < activation.index("battleInt->stackActivated(stack);"), \
        "the missing-window guard must precede activation"


check_missing_battle_interface_return(player)
# Negative witnesses cover the original fallthrough and loss of its command.
player_fallback = body(player, "void CPlayerInterface::activeStack(const BattleID & battleID, const CStack * stack) //called when it's turn of that stack")
fallback = body(player_fallback, "if(!battleInt)")
for broken_fallback in (fallback.replace("return;", ""),
                        fallback.replace("cb->battleMakeUnitAction(battleID, BattleAction::makeDefend(stack));", "")):
    try:
        check_missing_battle_interface_return(player.replace(fallback, broken_fallback, 1))
    except AssertionError:
        pass
    else:
        raise AssertionError("missing-battlefield source guard accepted broken recovery")

print("battle restart teardown source checks passed")
