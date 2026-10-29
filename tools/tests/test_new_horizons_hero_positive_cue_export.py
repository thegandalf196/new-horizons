#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Ordinary progression exporter wiring and opt-in bootstrap denials."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
FILTER = "NewHorizonsHeroPositiveCueFixtureExportTest.ExportOrdinaryProgressionAdventureSave"
PREFIX = "NH_EXPORT_HERO_POSITIVE_CUE_"


class HeroPositiveCueExportSourceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bootstrap = (ROOT / "test/CVcmiTestConfig.cpp").read_text()
        cls.exporter = (ROOT / "test/server/battles/NewHorizonsHeroPositiveCueFixtureExportTest.cpp").read_text()

    def test_early_exact_gate_preserves_old_hero_proc_guard(self):
        start = self.bootstrap.index('\tconst char * ordinaryExport =')
        end = self.bootstrap.index('\tconst char * skirmisherExport =', start)
        self.assertEqual(hashlib.sha256(self.bootstrap[start:end].encode()).hexdigest(),
                         "19ca6f1bbea9af95dd7925a8e9321984a0ca2173e46787caf76bab18f0bc0d83")
        allocate = self.bootstrap.index("LIBRARY = new GameLibrary;")
        self.assertIn(FILTER, self.bootstrap[:allocate])
        self.assertIn('!positiveCueExport || std::string_view(positiveCueExport) != "1"', self.bootstrap[:allocate])
        self.assertIn("useOrdinaryExport || useSkirmisherExport || usePositiveCueExport", self.bootstrap)
        self.assertLess(self.bootstrap.index("Ordinary hero progression export bootstrap requires"), allocate)
        registration = (ROOT / "test/CMakeLists.txt").read_text()
        self.assertEqual(registration.count("server/battles/NewHorizonsHeroPositiveCueFixtureExportTest.cpp"), 1)

    def test_canonical_starts_and_unvisited_normal_xp_inputs_only(self):
        for identity in ('"core:daremyth"', '"core:vey"'):
            self.assertIn(identity, self.exporter)
        self.assertEqual(self.exporter.count("rewardExperience(10000)"), 2)
        self.assertEqual(self.exporter.count(".heroExperience(0)"), 2)
        self.assertIn('.hero({21, 10, 0}, vey, PlayerColor(0)).heroExperience(0)', self.exporter)
        self.assertIn('player.compOnly = player.color != PlayerColor(0)', self.exporter)
        self.assertIn('ASSERT_EQ(world.players.at(player).human, color == 0)', self.exporter)
        self.assertIn('ASSERT_EQ(heroes.size(), color == 0 ? 2u : 1u)', self.exporter)
        for x in (18, 22):
            self.assertIn(f'.monster({{{x}, 14, 0}}, CreatureID(CreatureID::decode("core:archer")), 1,', self.exporter)
        self.assertEqual(self.exporter.count('static_cast<int8_t>(CGCreature::Character::SAVAGE)'), 2)
        self.assertIn('ASSERT_EQ(neutral->agression, 10)', self.exporter)
        self.assertIn('ASSERT_EQ(neutral->getStackCount(SlotID(0)), 1)', self.exporter)
        self.assertIn("ASSERT_FALSE(quest.isCompleted)", self.exporter)
        self.assertIn("ASSERT_EQ(quest.reward->reward.heroExperience, 10000)", self.exporter)
        self.assertIn("PlayerConnectionID::FIRST_HUMAN", self.exporter)
        for forbidden in ("overrideGameSetting(", "prepareOffer(", "levelUpHero(", "setSeed(",
                          "applyPerkSelection(", "onHeroVisit(", "giveExperience(",
                          ".heroPrimary(", ".heroSecondarySkills(", ".heroEquipped(",
                          ".heroGarrison(", ".heroSpells(", "forceMaximumDamage("):
            self.assertNotIn(forbidden, self.exporter)

    def test_real_roundtrip_and_output_isolation_precede_final_name(self):
        self.assertLess(self.exporter.index("handler->save(filename"), self.exporter.index("handler->load(loadInfo)"))
        self.assertLess(self.exporter.index("handler->load(loadInfo)"), self.exporter.index("fs::create_hard_link(pending, final)"))
        self.assertIn("server.gameState = handler->gs;", self.exporter)
        self.assertIn("ASSERT_NO_FATAL_FAILURE(checkOrdinaryWorld());", self.exporter)
        self.assertIn("saveRoot == privateRoot || !containsPath(privateRoot, saveRoot)", self.exporter)
        self.assertIn("VCMIDirs::get().dataPaths()", self.exporter)
        self.assertIn("VCMIDirs::get().userCachePath()", self.exporter)


@unittest.skipUnless(os.environ.get("NH_NATIVE_TEST_BINARY"), "Explicit native bootstrap-denial invocation required")
class HeroPositiveCueBootstrapDenialTest(unittest.TestCase):
    def test_partial_wrong_filter_and_mixed_exports_fail_before_library(self):
        binary = Path(os.environ["NH_NATIVE_TEST_BINARY"])
        self.assertTrue(binary.is_absolute() and binary.is_file())
        with tempfile.TemporaryDirectory(prefix="nh-positive-cue-denial-") as temporary:
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
            for suffix in ("SAVE_ROOT", "PRIVATE_ROOT", "SOURCE_ROOT"):
                cases.append(("empty_" + suffix, {**base, PREFIX + suffix: ""}, FILTER))
            for pattern in ("*", FILTER + ":OtherTest.*"):
                cases.append(("wrong_filter_" + pattern, dict(base), pattern))
            for mixed in ("NH_EXPORT_HERO_PROC_ORDINARY_BOOTSTRAP", "NH_EXPORT_HERO_PROC_FAMILY",
                          "NH_EXPORT_HERO_PROC_SAVE_ROOT", "NH_EXPORT_SKIRMISHER_PREVIEW_ORDINARY_BOOTSTRAP",
                          "NH_EXPORT_SKIRMISHER_PREVIEW_SAVE_ROOT", "NH_EXPORT_SKIRMISHER_PREVIEW_PRIVATE_ROOT",
                          "NH_EXPORT_SKIRMISHER_PREVIEW_SOURCE_ROOT"):
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

    def test_authored_input_partial_mixed_and_wrong_filter_denied_early(self):
        binary = Path(os.environ["NH_NATIVE_TEST_BINARY"])
        self.assertTrue(binary.is_absolute() and binary.is_file())
        prefix = "NH_EXPORT_HERO_CUE_AUTHORED_"
        selected = "NewHorizonsAuthoredHeroCueFixtureExportTest.ExportAuthoredEligibleAdventureSave"
        with tempfile.TemporaryDirectory(prefix="nh-authored-cue-denial-") as temporary:
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
                    prefix + "BOOTSTRAP": "1", prefix + "SAVE_ROOT": str(save),
                    prefix + "PRIVATE_ROOT": str(private), prefix + "SOURCE_ROOT": str(ROOT)}
            cases = []
            for suffix in ("BOOTSTRAP", "SAVE_ROOT", "PRIVATE_ROOT", "SOURCE_ROOT"):
                changed = dict(base)
                changed.pop(prefix + suffix)
                cases.append(("missing_" + suffix, changed, selected))
            for value in ("", "0", "true"):
                cases.append(("flag_" + value, {**base, prefix + "BOOTSTRAP": value}, selected))
            for suffix in ("SAVE_ROOT", "PRIVATE_ROOT", "SOURCE_ROOT"):
                cases.append(("empty_" + suffix, {**base, prefix + suffix: ""}, selected))
            for pattern in ("*", selected + ":Other.*", FILTER):
                cases.append(("filter_" + pattern, dict(base), pattern))
            for variable in ("NH_EXPORT_HERO_PROC_ORDINARY_BOOTSTRAP", "NH_EXPORT_HERO_PROC_FAMILY",
                             "NH_EXPORT_HERO_PROC_SAVE_ROOT", PREFIX + "ORDINARY_BOOTSTRAP"):
                cases.append(("mixed_" + variable, {**base, variable: "1"}, selected))
            for label, environment, pattern in cases:
                with self.subTest(label=label):
                    result = subprocess.run([str(binary), "--gtest_filter=" + pattern], cwd=root,
                                            env=environment, capture_output=True, text=True, timeout=10)
                    output = result.stdout + result.stderr
                    self.assertNotEqual(result.returncode, 0)
                    self.assertTrue(any(message in output for message in (
                        "Authored hero cue export requires its exact isolated invocation",
                        "Ordinary hero export bootstrap requires its exact isolated export invocation",
                        "Ordinary hero progression export bootstrap requires its exact isolated export invocation")), output)
                    self.assertNotIn("Initializing content handler", output)
                    self.assertFalse(any(save.iterdir()))


if __name__ == "__main__":
    unittest.main()
