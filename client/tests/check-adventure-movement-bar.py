#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Guard movement-bar scaling against the hero's real daily limit."""

from pathlib import Path


SOURCE = (Path(__file__).resolve().parents[1] / "adventureMap/CList.cpp").read_text()


def frame(remaining: int, limit: int, count: int) -> int:
    if count <= 1 or limit <= 0:
        return 0
    remaining = min(max(remaining, 0), limit)
    return remaining * (count - 1) // limit


def main():
    body = SOURCE.split("void CHeroList::CHeroItem::update()", 1)[1].split(
        "std::shared_ptr<CIntObject> CHeroList::CHeroItem::genSelection", 1
    )[0]
    for token in (
        "hero->movementPointsLimit()",
        "std::clamp(hero->movementPointsRemaining(), 0, std::max(0, movementLimit))",
        "static_cast<uint64_t>(movementRemaining) * (movementFrames - 1) / movementLimit",
        "movement->setFrame(movementFrame)",
    ):
        assert token in body, token
    assert "movementPointsRemaining() / 100" not in body

    # New Horizons daily totals are around 200-260 rather than legacy UI's
    # assumed 100 points per frame. Full, half, empty and overflow all scale.
    assert frame(200, 200, 25) == 24
    assert frame(130, 260, 25) == 12
    assert frame(0, 240, 25) == 0
    assert frame(999, 240, 25) == 24
    assert frame(100, 0, 25) == 0
    print("PASS: adventure movement bar scales current/maximum across all animation frames")


if __name__ == "__main__":
    main()
