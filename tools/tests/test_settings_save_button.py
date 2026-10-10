"""Save-only two-frame settings-button data and existing input guard contracts."""
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SettingsSaveButtonTest(unittest.TestCase):
    def setUp(self):
        config = json.loads((ROOT / "config/widgets/settings/settingsMainContainer.json").read_text())
        self.buttons = {item["name"]: item for item in config["items"] if item.get("type") == "button"}

    def test_save_uses_only_existing_two_frames(self):
        save = self.buttons["saveButton"]
        self.assertEqual(save["image"], "SOSAVE.DEF")
        self.assertEqual(save["imageOrder"], [1, 0, 1, 1])
        self.assertTrue(all(type(frame) is int and 0 <= frame < 2 for frame in save["imageOrder"]))

    def test_save_action_layout_and_other_button_mappings_are_preserved(self):
        save = self.buttons["saveButton"]
        self.assertEqual(save["position"], {"x": 490, "y": 306})
        self.assertEqual(save["callback"], "saveGame")
        self.assertEqual(save["hotkey"], "settingsSaveGame")
        self.assertEqual(save["help"], "core.help.322")
        for name in ("loadButton", "restartButton", "mainMenuButton", "quitButton"):
            with self.subTest(button=name):
                self.assertEqual(self.buttons[name]["imageOrder"], [1, 0, 2, 3])

    def test_disabled_save_still_uses_existing_battle_and_input_guards(self):
        settings = (ROOT / "client/windows/settings/SettingsMainWindow.cpp").read_text()
        self.assertIn("saveButton->block(GAME->server().isGuest() || parentBattleUi);", settings)
        buttons = (ROOT / "client/widgets/Buttons.cpp").read_text()
        blocked = buttons.split("void CButton::setState(EButtonState newState)", 1)[1].split("EButtonState ButtonBase::getState()", 1)[0]
        self.assertIn("if (newState == EButtonState::BLOCKED)", blocked)
        self.assertIn("removeUsedEvents(LCLICK | SHOW_POPUP | HOVER | KEYBOARD)", blocked)


if __name__ == "__main__":
    unittest.main()
