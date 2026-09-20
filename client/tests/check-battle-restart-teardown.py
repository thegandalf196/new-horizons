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

print("battle restart teardown source checks passed")
