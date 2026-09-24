#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Source guard: daily Adventure Spell state invalidates cached client paths."""

from pathlib import Path


def verify(header: str, source: str):
    signature = "visitSetNewHorizonsAdventureSpellState(SetNewHorizonsAdventureSpellState & pack)"
    assert f"void {signature} override;" in header
    body = source.split(f"void ApplyClientNetPackVisitor::{signature}", 1)[1].split(
        "void ApplyClientNetPackVisitor::visitSetMovePoints", 1
    )[0]
    assert "callAllInterfaces(cl, &CGameInterface::invalidatePaths);" in body
    assert "headless" not in body, "Path invalidation must not depend on graphical mode"
    assert "calculatePaths" not in body, "Invalidate lazily; do not recalculate on every state packet"


def main():
    root = Path(__file__).resolve().parents[2]
    header = (root / "client/ClientNetPackVisitors.h").read_text()
    source = (root / "client/NetPacksClient.cpp").read_text()
    verify(header, source)
    signature = "void ApplyClientNetPackVisitor::visitSetNewHorizonsAdventureSpellState"
    before, method = source.split(signature, 1)
    body, after = method.split("void ApplyClientNetPackVisitor::visitSetMovePoints", 1)
    invalidation = "callAllInterfaces(cl, &CGameInterface::invalidatePaths);"
    for replacement in ("", "if(headless) return;" + invalidation, invalidation + "calculatePaths();"):
        mutated = body.replace(invalidation, replacement)
        try:
            verify(header, before + signature + mutated + "void ApplyClientNetPackVisitor::visitSetMovePoints" + after)
        except AssertionError:
            continue
        raise AssertionError("Invalidation regression was not detected")
    print("PASS: authoritative daily Adventure Spell state invalidates client paths lazily; 3 mutants rejected")
    print("Source wiring only; not compiled or runtime cache evidence")


if __name__ == "__main__":
    main()
