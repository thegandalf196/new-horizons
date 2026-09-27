#!/usr/bin/env python3
"""Static regression guard for the Shift-split over-cap dialog crash.

The exact-build reproduction showed that a callback can push an info window
before CSplitWindow closes. Keep callback state and values local, then remove
the split window before invoking user-facing callback code.
"""

from pathlib import Path
import re


SOURCE = Path(__file__).resolve().parents[1] / "windows/GUIClasses.cpp"


def main() -> None:
    source = SOURCE.read_text()
    method = source.split("void CSplitWindow::apply()", 1)[1]
    method = method.split("void CSplitWindow::sliderMoved", 1)[0]
    method = re.sub(r"//[^\n]*|/\*.*?\*/", "", method, flags=re.S)
    compact = re.sub(r"\s+", "", method)

    snapshot = "autocallbackToRun=std::exchange(callback,nullptr);"
    left_value = "constintleft=leftAmount;"
    right_value = "constintright=rightAmount;"
    close = "close();"
    invoke = "if(callbackToRun)callbackToRun(left,right);"
    for expected in (snapshot, left_value, right_value, close, invoke):
        assert expected in compact, f"CSplitWindow::apply missing {expected}"

    assert compact.index(snapshot) < compact.index(close)
    assert compact.index(left_value) < compact.index(close)
    assert compact.index(right_value) < compact.index(close)
    assert compact.index(close) < compact.index(invoke)


if __name__ == "__main__":
    main()
