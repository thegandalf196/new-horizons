#!/usr/bin/env python3
"""Prevent invalid battle positions from being mistaken for siege towers."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "client" / "battle" / "BattleStacksController.cpp"


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    semantic_guard = "stack->initialPosition.isTower() && stack->isTurret()"

    if source.count(semantic_guard) != 2:
        raise SystemExit(
            "Battle stack rendering must validate both turret identity and a real tower hex"
        )

    if "initialPosition < 0" in source:
        raise SystemExit(
            "Negative battle positions include INVALID and must not identify siege towers"
        )


if __name__ == "__main__":
    main()
