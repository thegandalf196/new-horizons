"""Asset/process-free source boundaries; not compilation or rendered acceptance."""
import hashlib
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BootstrapSourceTests(unittest.TestCase):
    def test_driver_only_routes_after_unchanged_deadline_tick(self):
        source = (ROOT/"client/tests/CureIntegrationDriver.cpp").read_text()
        additions = ("#include \"CureIntegrationBootstrap.h\"\n",
                     '\tCureIntegrationBootstrap bootstrap{std::getenv("NH_CURE_BOOTSTRAP")};\n',
                     "\t\tstate->bootstrap.update();\n")
        self.assertLess(source.index("state->tick(state);"), source.index("state->bootstrap.update();"))
        for addition in additions:
            self.assertEqual(source.count(addition), 1)
            source = source.replace(addition, "")
        self.assertEqual(hashlib.sha256(source.encode()).hexdigest(),
                         "bb671639f5dbb209c7c5851122b2e937d51ad3a7ee8bf796e8babbefb9c1b720")

    def test_only_dedicated_target_compiles_adapter(self):
        cmake = (ROOT/"clientapp/CMakeLists.txt").read_text()
        integration = cmake.split("if(ENABLE_NH_CURE_CLIENT_INTEGRATION)", 1)[1]
        for filename in ("CureIntegrationBootstrap.cpp", "CureIntegrationBootstrap.h"):
            self.assertEqual(cmake.count(filename), 1)
            self.assertIn(filename, integration)
        self.assertIn("NH_CURE_CLIENT_INTEGRATION", integration)
        header = (ROOT/"client/tests/CureIntegrationBootstrap.h").read_text()
        self.assertIn("#ifndef NH_CURE_CLIENT_INTEGRATION\n#error", header)

    def test_ordinary_once_route_has_no_gameplay_or_timer_override(self):
        source = (ROOT/"client/tests/CureIntegrationBootstrap.cpp").read_text()
        steps = ("if(state.consume())", "auto info = fixedMap();",
                 "CMainMenu::openLobby(ESelectionScreen::newGame, true, {}, ELoadMode::NONE, false, false);",
                 "GAME->server().startMapAfterConnection(info);")
        self.assertEqual([source.index(step) for step in steps], sorted(source.index(step) for step in steps))
        for forbidden in ("debugStartTest(", "setPlayer(", "sendStartGame(", "std::thread",
                          "sleep_for", "newHorizonsFrozen", "seconds(180)", "srand("):
            self.assertNotIn(forbidden, source)
        self.assertIn("state.awaitingLoaded()", source)
        self.assertIn("EClientState::GAMEPLAY", source)
        self.assertIn("state.acceptLoaded();", source)

    def test_fixed_profile_provenance_and_actual_world_are_separate(self):
        source = (ROOT/"client/tests/CureIntegrationBootstrap.cpp").read_text()
        for expected in ("getResourceName(resource)", "userDataPath()", "boost::filesystem::canonical(expected)",
                         "nhart::sha256", "validateHeader(*info->mapHeader)", "getInitialStartInfo()",
                         "player.isControlledByHuman()", "player.isControlledByAI()", "threeRedStacks(*hero)",
                         "PlayerColor::NEUTRAL", 'decode("core:iceElemental")'):
            self.assertIn(expected, source)

    def test_pure_state_test_is_conditional_not_default_nine_lane(self):
        cmake = (ROOT/"test/CMakeLists.txt").read_text()
        block = cmake.split("if(ENABLE_NH_CURE_CLIENT_INTEGRATION)", 1)[1].split("endif()", 1)[0]
        self.assertIn("nhCureIntegrationBootstrapStateTest", block)
        self.assertIn('LABELS "nh-integration;state"', block)


if __name__ == "__main__":
    unittest.main()
