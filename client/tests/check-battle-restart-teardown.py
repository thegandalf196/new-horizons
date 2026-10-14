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

print("battle restart teardown source checks passed")
