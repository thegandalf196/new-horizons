#!/usr/bin/env python3
"""Scoped Reach cursor lifecycle checks; not compiled or rendered acceptance."""
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index("{", source.index(signature))
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start + 1:end - 1]


def guarded_refresh(body, prefix):
    assert body.count("setReach(") == 1
    match = re.search(r"if\((" + re.escape(prefix) + r"\b[^{}]*)\)\s*\{([^{}]*setReach\([^{}]*)\}", body)
    assert match is not None
    assert "||" not in match.group(1)
    return match.group(2)


def check():
    source = (ROOT / "client/gui/CursorHandler.cpp").read_text()
    update = function(source, "void CursorHandler::update()")
    resize = function(source, "void CursorHandler::onScreenResize()")
    drag = function(source, "void CursorHandler::dragAndDropCursor(std::shared_ptr<IImage> image)")
    apply = function(source, "void CursorHandler::applyCursorImage()")
    normal = function(source, "void CursorHandler::set(const std::string & index)")

    # Neither automatic route may invoke setReach's no-drag precondition while
    # a drag image temporarily overrides the still-selected Reach cursor.
    for body in (update, resize):
        guarded_refresh(body, "currentReachCursor && !dndObject")
    assert resize.count("currentReachCursor.reset();") == 1
    assert "currentReachCursor.reset();" in guarded_refresh(resize, "currentReachCursor && !dndObject")

    # Resize retains the deferred selection and marks its raster stale. Ending
    # the drag rebuilds that image before the normal backend application resumes.
    assert re.search(r"if\(currentReachCursor\)\s*//[^\n]*\n\s*reachCursorRasterScaling = 0;", resize)
    assert drag.index("dndObject = image;") < drag.index("if(!dndObject && currentReachCursor")
    assert "reachCursorRasterScaling != ENGINE->screenHandler().getScalingFactor()" in drag
    guarded_refresh(drag, "!dndObject && currentReachCursor")
    assert drag.index("setReach(") < drag.index("applyCursorImage();")
    assert re.search(r"setReach\(previous.first, previous.second\);\s*return;", drag)

    # A selected Reach cursor must not scale a different drag image's hotspot.
    assert re.search(r"if\(currentReachCursor && !dndObject\)\s*reachCursor::Frame", apply)
    assert "else\n\t\tcursor->setImage(getCurrentImage(), getPivotOffset());" in apply
    assert "index == currentCursorID && !currentReachCursor" in normal
    assert normal.index("currentReachCursor.reset();") < normal.index("currentCursorID = index;")


if __name__ == "__main__":
    check()
    print("PASS: Reach cursor drag, deferred raster refresh and normal-cursor reset source contract")
    print("NOT compiled, native cursor-backend, or rendered acceptance")
