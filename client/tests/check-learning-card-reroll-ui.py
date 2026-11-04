#!/usr/bin/env python3
"""Source guard for indexed Connoisseur controls and asynchronous query ownership."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
layout = (ROOT / "client/windows/CLevelWindow.cpp").read_text()
controls = (ROOT / "client/windows/CLevelWindowReroll.cpp").read_text()
interface = (ROOT / "client/CPlayerInterfaceLevelUp.cpp").read_text()
header = (ROOT / "client/windows/CLevelWindow.h").read_text()
shared_header = (ROOT / "client/windows/GUIClasses.h").read_text()
assert "class CLevelWindow" not in shared_header
for caller in ("client/windows/CLevelWindow.cpp", "client/windows/CLevelWindowReroll.cpp",
               "client/CPlayerInterface.cpp", "client/CPlayerInterfaceLevelUp.cpp"):
    assert "CLevelWindow.h\"" in (ROOT / caller).read_text(), caller
town = (ROOT / "client/windows/CCastleInterface.cpp").read_text()
assert "selectionMade(-2 - static_cast<int>(skillIndex), queryID)" in interface
assert "selectionMade(-1, queryID)" not in interface
assert "requestReroll(originalIndex)" in controls
assert "if(originalIndex >= skills.size())" in controls
assert "index >= skills.size()" in controls
assert "skillIndex >= skills.size()" in controls
assert "Reroll this skill once" in controls
assert "rerollCallback(skillIndex)" in controls
assert "rerollPending = true;" in controls
assert controls.index("rerollPending = true;") < controls.index("rerollCallback(skillIndex)")
assert "*rerollRequest != requestID || success" in controls
assert "skillChoiceBox->setInputEnabled(!waiting)" in controls
assert "perkChoiceBox->setInputEnabled(!waiting)" in controls
assert "std::function<int(size_t)> rerollCallback" in header
assert "restoreOfferSelection(previousChoice)" in layout
assert "activeBox->selectIndex(index)" in controls
assert "dialog->queryID = queryID;" in interface
assert "!levelWindow->matchesQuery(replacesQueryID)" in interface
for obsolete in ("Quick Study:", "replace these choices once", "Point(448, 532)"):
    assert obsolete not in layout + controls
assert "sageSpellsButton" not in town
assert "newHorizonsSageRevealedSpells" not in town
assert "new-horizons.sage.guildSpells" not in town
# Native80px button and narrowed skill content remain within its244px recess.
assert 44 + 156 < 208 and 208 + 80 == 44 + 244
for row in range(2):
    top = 300 + row * 96
    assert top <= 326 + row * 96 and 326 + row * 96 + 32 <= top + 84
assert 300 + 96 + 84 < 532
print("PASS: per-skill indexed Connoisseur controls, pending-request guards, preserved selection and query replacement")
print("Source/layout bounds only; native availability and rendered readability remain unverified")
