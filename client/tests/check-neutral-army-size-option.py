#!/usr/bin/env python3
"""Source/layout contract for the scenario-only neutral-size control.

These checks do not construct widgets or establish rendered acceptance.
"""
from pathlib import Path
import json
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def body(source, signature):
    match = re.search(re.escape(signature) + r"\s*\{", source)
    if match is None:
        raise AssertionError("Missing callback: " + signature)
    start = match.end()
    depth = 1
    index = start
    while depth:
        depth += (source[index] == "{") - (source[index] == "}")
        index += 1
    return source[start:index - 1]


def check_silent_slider_refresh(source, slider_source):
    refresh = body(source, "void ExtraOptionsTab::recreate(bool campaign)")
    assert "slider->scrollTo(percent - 1, false);" in refresh
    assert "slider->setValue(" not in refresh
    # CSlider::setValue currently ignores its arguments. The existing scrollTo
    # API updates the requested position and alone provides the silent flag.
    scroll = body(slider_source, "void CSlider::scrollTo(int to, bool callCallbacks)")
    assignment = scroll.index("value = to;")
    update = scroll.index("updateSliderPos();")
    callback = scroll.index("moved(getValue());")
    assert assignment < update < callback
    assert re.search(r"if\s*\(callCallbacks\)\s*moved\(getValue\(\)\);", scroll)


def check_read_only_slider_events(source, object_source):
    refresh = body(source, "void ExtraOptionsTab::recreate(bool campaign)")
    assert "const bool editable = canChangeNeutralArmySize();" in refresh
    assert "slider->block(!editable);" in refresh
    assert "slider->setInputEnabled(editable);" in refresh
    assert "slider->setEnabled(" not in refresh
    assert "slider->disable(" not in refresh
    setter = body(object_source, "void CIntObject::setInputEnabled(bool on)")
    assert "inputEnabled = on;" in setter
    assert re.search(r"if\s*\(on\)\s*activateEvents\(used\);\s*else\s*deactivateEvents\(used\);", setter)
    assert re.search(r"for\(auto & elem : children\)\s*elem->setInputEnabled\(on\);", setter)
    assert "recActions =" not in setter


class NeutralArmySizeOptionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "client/lobby/ExtraOptionsTab.cpp").read_text()
        layout = json.loads((ROOT / "config/widgets/neutralArmySizeOptions.json").read_text())
        cls.widgets = {item["name"]: item for item in layout["items"]}
        cls.text = json.loads((ROOT / "Mods/vcmi/Content/config/translations/english.json").read_text())

    def test_slider_covers_every_admitted_percentage_without_zero_or_amplification(self):
        slider = self.widgets["sliderNeutralArmySize"]
        self.assertEqual(slider["itemsVisible"], 1)
        self.assertEqual(slider["itemsTotal"], 100)
        percentages = [index + 1 for index in range(slider["itemsTotal"] - slider["itemsVisible"] + 1)]
        self.assertEqual(percentages, list(range(1, 101)))
        self.assertEqual(slider["selected"] + 1, 50)
        self.assertEqual(slider["callback"], "setNeutralArmySize")
        self.assertEqual(slider["style"], "blue")

    def test_request_is_validated_and_sent_through_existing_lobby_api(self):
        callback = body(self.source, "void ExtraOptionsTab::setNeutralArmySize(int index)")
        checks = ["!canChangeNeutralArmySize()", "index < 0", "index >= 100"]
        for check in checks:
            self.assertIn(check, callback)
        steps = ["auto info = SEL->getStartInfo()->extraOptionsInfo;",
                 "info.neutralArmySizePercent = index + 1;", "info.validateNeutralArmySize();",
                 "GAME->server().setExtraOptionsInfo(info);"]
        positions = [callback.index(step) for step in steps]
        self.assertEqual(positions, sorted(positions))
        self.assertEqual(callback.count("setExtraOptionsInfo(info)"), 1)
        self.assertNotIn("overrideGameSetting", self.source)
        self.assertNotIn("initializeWorld", self.source)

    def test_opening_refresh_is_read_only_and_loaded_games_cannot_submit(self):
        constructor = body(self.source, "ExtraOptionsTab::ExtraOptionsTab()\n\t: OptionsTabBase(JsonPath::builtin(\"config/widgets/extraOptionsTab.json\"))")
        self.assertLess(constructor.index('addCallback("setNeutralArmySize"'), constructor.index("build(JsonNode"))
        refresh = body(self.source, "void ExtraOptionsTab::recreate(bool campaign)")
        for text in ["setExtraOptionsInfo", "neutralArmySizePercent ="]:
            self.assertNotIn(text, constructor + refresh)
        self.assertIn("slider->scrollTo(percent - 1, false);", refresh)
        self.assertIn("slider->block(!editable);", refresh)
        gate = body(self.source, "bool ExtraOptionsTab::canChangeNeutralArmySize() const")
        for text in ["ESelectionScreen::newGame", "EStartMode::NEW_GAME", "isHost()", "!GAME->server().battleMode"]:
            self.assertIn(text, gate)
        self.assertIn("EStartMode::LOAD_GAME", refresh)
        self.assertIn('"vcmi.optionsTab.neutralArmySize.saved"', refresh)

    def test_refresh_uses_actual_silent_slider_position_contract(self):
        slider = (ROOT / "client/widgets/Slider.cpp").read_text()
        check_silent_slider_refresh(self.source, slider)
        # Reject the previously proposed wrong API, a non-updating slider and
        # a callback guard that would send a request merely from readback.
        with self.assertRaises(AssertionError):
            check_silent_slider_refresh(self.source.replace(
                "slider->scrollTo(percent - 1, false);", "slider->setValue(percent - 1, false);"), slider)
        with self.assertRaises(ValueError):
            check_silent_slider_refresh(self.source, slider.replace("value = to;", "value = value;"))
        with self.assertRaises(AssertionError):
            check_silent_slider_refresh(self.source, slider.replace("if (callCallbacks)", "if (true)"))

    def test_read_only_events_are_disabled_and_reenabled_without_hiding(self):
        objects = (ROOT / "client/gui/CIntObject.cpp").read_text()
        check_read_only_slider_events(self.source, objects)
        # The block-only proposal left wheel/keyboard/drag events registered.
        with self.assertRaises(AssertionError):
            check_read_only_slider_events(self.source.replace("slider->setInputEnabled(editable);", ""), objects)
        with self.assertRaises(AssertionError):
            check_read_only_slider_events(self.source.replace("slider->setInputEnabled(editable);",
                                                              "slider->setEnabled(editable);"), objects)
        with self.assertRaises(AssertionError):
            check_read_only_slider_events(self.source, objects.replace("elem->setInputEnabled(on);", ""))

    def test_layout_and_text_keep_readback_and_scope_visible(self):
        self.assertEqual(set(self.widgets), {"labelNeutralArmySize", "labelNeutralArmySizeValue",
                                           "sliderNeutralArmySize", "labelNeutralArmySizeHelp"})
        for item in self.widgets.values():
            if "text" in item:
                self.assertIn(item["text"], self.text)
        self.assertFalse(self.widgets["labelNeutralArmySizeHelp"]["adoptHeight"])
        self.assertEqual(self.text["vcmi.optionsTab.neutralArmySize.default"], "Map/default")
        self.assertIn("%d", self.text["vcmi.optionsTab.neutralArmySize.help"])
        for wording in ["maps may override", "round up", "existing saves"]:
            self.assertIn(wording, self.text["vcmi.optionsTab.neutralArmySize.help"])


if __name__ == "__main__":
    unittest.main()
