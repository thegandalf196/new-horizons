#!/usr/bin/env python3
"""Guard the playable Leadership empty-slot repair and split-dialog labels."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SERVER = (ROOT / "server/CGameHandler.cpp").read_text(encoding="utf-8")
GARRISON = (ROOT / "client/widgets/CGarrisonInt.cpp").read_text(encoding="utf-8")
SPLIT_CPP = (ROOT / "client/windows/GUIClasses.cpp").read_text(encoding="utf-8")
SPLIT_H = (ROOT / "client/windows/GUIClasses.h").read_text(encoding="utf-8")
SERVER_TEST = (ROOT / "test/server/NewHorizonsLeadershipAdmissionTest.cpp").read_text(encoding="utf-8")


def require(source: str, fragment: str, message: str) -> None:
    assert fragment in source, message


require(
    SERVER,
    "whole-stack drag into an empty hero slot",
    "server must distinguish an empty-slot move intent from an exact numeric split",
)
require(
    SERVER,
    "return moveStack(sourceLocation, destinationLocation, capacity->maximum);",
    "server must authoritatively transfer the maximum Leadership-legal count",
)
require(
    GARRISON,
    "The server treats a whole-stack drag into an empty hero slot",
    "client fast path must allow the server-authored partial transfer",
)
require(GARRISON, 'return "Hero: " + name;', "split dialog must identify hero side")
require(
    GARRISON,
    '"Garrison: " + name',
    "split dialog must identify garrison side",
)
split_window_declaration = SPLIT_H.split("class CSplitWindow", 1)[1].split("class CLevelWindow", 1)[0]
require(split_window_declaration, "std::shared_ptr<CLabel> leftOwner;", "split window must retain its left owner label")
require(split_window_declaration, "std::shared_ptr<CLabel> rightOwner;", "split window must retain its right owner label")
require(SPLIT_CPP, "leftOwner = std::make_shared<CLabel>", "left owner label must be rendered")
require(SPLIT_CPP, "rightOwner = std::make_shared<CLabel>", "right owner label must be rendered")
require(
    SERVER_TEST,
    "WholeStackDragIntoEmptyHeroSlotFillsLeadershipCapacityAndLeavesRemainder",
    "authoritative regression must cover the reported empty-slot transfer",
)
require(
    SERVER_TEST,
    "garrison->setVisitingHero(hero);",
    "cross-army regression must establish a legal exchange relationship",
)
require(
    SERVER_TEST,
    "ArrangeStacks drag(1, SlotID(0), SlotID(0), hero->id, garrison->id, 0);",
    "regression must use the UI's empty-destination-first swap orientation",
)
require(
    SERVER_TEST,
    "ArrangeStacks emptyDrag(1, SlotID(0), SlotID(0), hero->id, garrison->id, 0);",
    "empty-source regression must exercise the guarded hero-destination branch",
)

print("Leadership empty-slot partial transfer and split owner-label guards passed.")
