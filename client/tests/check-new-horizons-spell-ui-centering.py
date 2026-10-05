#!/usr/bin/env python3
"""Source guard for New Horizons spell modal and icon placement."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPELL_WINDOW = (ROOT / "client/windows/CSpellWindow.cpp").read_text(encoding="utf-8")
TEMPORAL_WINDOW = (ROOT / "client/battle/TemporalFieldWindow.cpp").read_text(encoding="utf-8")
DISPEL_WINDOW = (ROOT / "client/battle/SelectiveDispelWindow.cpp").read_text(encoding="utf-8")


def require(source: str, value: str, label: str) -> None:
    if value not in source:
        raise AssertionError(f"missing {label}: {value}")


def check_centered_modal(source: str, class_name: str) -> None:
    constructor = source.split(f"{class_name}::{class_name}(", 1)[1].split("OBJECT_CONSTRUCTION;", 1)[0]
    require(constructor, "pos.w = WINDOW_WIDTH;", f"{class_name} width initialization")
    require(constructor, "pos.h = WINDOW_HEIGHT;", f"{class_name} height initialization")
    require(constructor, "center();", f"{class_name} viewport centering")
    for obsolete in ("moveTo(context.anchor", "fitToScreen(4)"):
        if obsolete in constructor:
            raise AssertionError(f"{class_name} still uses cursor-relative positioning: {obsolete}")


def check() -> None:
    set_spell = SPELL_WINDOW.split("void CSpellWindow::SpellArea::setSpell(", 1)[1].split(
        "void CSpellWindow::SpellArea::showPopupWindow", 1
    )[0]
    require(set_spell, "image->moveTo(pos.topLeft());", "restore original slot origin before changing spell")
    require(set_spell, "image->setFrame(mySpell->id.getNum());", "runtime SPELLS frame selection")
    assert set_spell.index("image->moveTo(pos.topLeft());") < set_spell.index("image->setFrame(mySpell->id.getNum());")
    require(set_spell, "if(!mySpell->getIconBook().empty())", "dynamic icon-only placement guard")
    require(set_spell, "Rect iconCanvas(pos.x, pos.y, originalIconCanvasWidth, originalIconCanvasHeight);",
            "native 78x65 fallback canvas")
    require(set_spell, "iconCanvas = schoolBorder->pos;", "loaded school-border canvas")
    require(set_spell, "iconCanvas.w - image->pos.w", "runtime icon-width centering")
    require(set_spell, "iconCanvas.h - image->pos.h", "runtime icon-height centering")
    require(set_spell, "image->moveTo(Point(", "dynamic frame placement")

    check_centered_modal(TEMPORAL_WINDOW, "TemporalFieldWindow")
    check_centered_modal(DISPEL_WINDOW, "SelectiveDispelWindow")

    print("PASS: dynamic spell icons center within their loaded canvas; legacy DEF placement is preserved")
    print("PASS: Temporal Field and Selective Dispel choices center in the viewport without adding interaction steps")
    print("Source guard only; rendered layout remains unverified")


if __name__ == "__main__":
    check()
