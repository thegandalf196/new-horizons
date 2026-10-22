#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Authored preview export wiring and explicit opt-in early-bootstrap denials."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
FILTER = "NewHorizonsSkirmisherPreviewFixtureExportTest.ExportAuthoredPreinitAdventureSave"
PREFIX = "NH_EXPORT_SKIRMISHER_PREVIEW_"


class SkirmisherPreviewExportSourceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bootstrap = (ROOT / "test/CVcmiTestConfig.cpp").read_text()
        cls.exporter = (ROOT / "test/server/battles/NewHorizonsSkirmisherPreviewFixtureExportTest.cpp").read_text()

    def test_existing_hero_proc_guard_is_literal_and_both_gates_precede_library(self):
        start = self.bootstrap.index('\tconst char * ordinaryExport =')
        end = self.bootstrap.index('\tconst char * skirmisherExport =', start)
        block = self.bootstrap[start:end]
        self.assertEqual(hashlib.sha256(block.encode()).hexdigest(), "19ca6f1bbea9af95dd7925a8e9321984a0ca2173e46787caf76bab18f0bc0d83")
        gate = self.bootstrap.index("if(useSkirmisherExport)")
        denial = self.bootstrap.index("Ordinary Skirmisher export bootstrap requires its exact isolated export invocation")
        allocate = self.bootstrap.index("LIBRARY = new GameLibrary;")
        self.assertLess(gate, denial)
        self.assertLess(denial, allocate)
        self.assertIn(FILTER, self.bootstrap[:allocate])
        self.assertIn('!skirmisherExport || std::string_view(skirmisherExport) != "1"', self.bootstrap[:allocate])
        self.assertIn('std::getenv("NH_EXPORT_HERO_PROC_FAMILY")', self.bootstrap[:allocate])
        self.assertIn('std::getenv("NH_EXPORT_HERO_PROC_SAVE_ROOT")', self.bootstrap[:allocate])

    def test_shipping_bootstrap_and_exact_registration(self):
        self.assertIn("const bool useOrdinaryBootstrap = useOrdinaryExport || useSkirmisherExport;", self.bootstrap)
        self.assertIn("initializeFilesystem(false, /*useTestPreset*/ !useOrdinaryBootstrap)", self.bootstrap)
        self.assertIn('return mod != "core" && mod != "vcmi" && mod != "new-horizons";', self.bootstrap)
        self.assertLess(self.bootstrap.index("requires only the shipping mods"), self.bootstrap.index("initializeLibrary()"))
        registration = (ROOT / "test/CMakeLists.txt").read_text()
        self.assertEqual(registration.count("server/battles/NewHorizonsSkirmisherPreviewFixtureExportTest.cpp"), 1)

    def test_only_preinit_full_rules_override_and_no_runtime_mutation(self):
        self.assertIn("TinyMapGameTest::mapLoaded(loaded);", self.exporter)
        self.assertIn("loaded->getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS)", self.exporter)
        self.assertEqual(self.exporter.count("loaded->overrideGameSetting("), 1)
        self.assertIn("ASSERT_EQ(restored, installedHeroes)", self.exporter)
        self.assertIn("validateWeightedPerkCapture(authoredHeroes, installedPerks)", self.exporter)
        for forbidden in ("applyPerkSelection(", "prepareOffer(", "levelUpHero(", "setSeed(",
                          "forceMaximumDamage(", "setPrimSkill(", "setSecSkillLevel(",
                          ".heroSecondarySkills(", ".heroPrimary(", ".heroEquipped(",
                          "makePlayerBattleAction(", "BattleStart", "battlecraftOverwatchReadyRound ="):
            self.assertNotIn(forbidden, self.exporter)
        self.assertIn("PlayerConnectionID::FIRST_HUMAN", self.exporter)
        self.assertIn("capacity->accepts(hero.getStackCount(SlotID(slot)))", self.exporter)

    def test_full_roundtrip_precedes_exclusive_final_output(self):
        self.assertLess(self.exporter.index("handler->save(filename"), self.exporter.index("handler->load(loadInfo)"))
        self.assertLess(self.exporter.index("handler->load(loadInfo)"), self.exporter.index("fs::create_hard_link(pending, final)"))
        self.assertIn("server.gameState = handler->gs;", self.exporter)
        self.assertIn("ASSERT_NO_FATAL_FAILURE(checkAdventureTurn());", self.exporter)
        self.assertIn("fs::symlink_status(saveRoot / name).type() != fs::file_type::not_found", self.exporter)
        self.assertIn("saveRoot == privateRoot || !containsPath(privateRoot, saveRoot)", self.exporter)
        self.assertIn("rejectOverlap(privateRoot, sourceRoot)", self.exporter)
        self.assertIn("VCMIDirs::get().dataPaths()", self.exporter)
        self.assertIn("VCMIDirs::get().binaryPath()", self.exporter)


@unittest.skipUnless(os.environ.get("NH_NATIVE_TEST_BINARY"), "Explicit native bootstrap-denial invocation required")
class SkirmisherPreviewBootstrapDenialTest(unittest.TestCase):
    def test_partial_wrong_filter_and_mixed_invocations_fail_before_library(self):
        binary = Path(os.environ["NH_NATIVE_TEST_BINARY"])
        self.assertTrue(binary.is_absolute() and binary.is_file())
        with tempfile.TemporaryDirectory(prefix="nh-export-denial-") as temporary:
            root = Path(temporary)
            private = root / "private"
            save = private / "Saves"
            save.mkdir(parents=True)
            base = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C.UTF-8",
                    "HOME": str(root / "home"), "XDG_CONFIG_HOME": str(root / "config"),
                    "XDG_CACHE_HOME": str(root / "cache"), "XDG_DATA_HOME": str(root / "data"),
                    "XDG_STATE_HOME": str(root / "state"), "XDG_RUNTIME_DIR": str(root / "runtime"),
                    "SDL_AUDIODRIVER": "dummy", "SDL_AUDIO_DRIVER": "dummy",
                    "SDL_VIDEODRIVER": "dummy", "SDL_VIDEO_DRIVER": "dummy",
                    PREFIX + "ORDINARY_BOOTSTRAP": "1", PREFIX + "SAVE_ROOT": str(save),
                    PREFIX + "PRIVATE_ROOT": str(private), PREFIX + "SOURCE_ROOT": str(ROOT)}
            cases = []
            for suffix in ("ORDINARY_BOOTSTRAP", "SAVE_ROOT", "PRIVATE_ROOT", "SOURCE_ROOT"):
                changed = dict(base)
                changed.pop(PREFIX + suffix)
                cases.append(("missing_" + suffix, changed, FILTER))
            for value in ("", "0", "true"):
                cases.append(("bad_flag_" + value, {**base, PREFIX + "ORDINARY_BOOTSTRAP": value}, FILTER))
            cases.append(("empty_root", {**base, PREFIX + "SAVE_ROOT": ""}, FILTER))
            for pattern in ("*", FILTER + ":OtherTest.*"):
                cases.append(("wrong_filter_" + pattern, dict(base), pattern))
            for mixed in ("NH_EXPORT_HERO_PROC_ORDINARY_BOOTSTRAP", "NH_EXPORT_HERO_PROC_FAMILY", "NH_EXPORT_HERO_PROC_SAVE_ROOT"):
                cases.append(("mixed_" + mixed, {**base, mixed: "1"}, FILTER))
            for label, environment, pattern in cases:
                with self.subTest(label=label):
                    result = subprocess.run([str(binary), "--gtest_filter=" + pattern], cwd=root,
                                            env=environment, capture_output=True, text=True, timeout=10)
                    output = result.stdout + result.stderr
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("export bootstrap requires its exact isolated export invocation", output)
                    self.assertNotIn("Initializing content handler", output)
                    self.assertFalse(any(save.iterdir()))


if __name__ == "__main__":
    unittest.main()
