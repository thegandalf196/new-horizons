"""Current skill, perk and hero-spell art resolves through the shipping package."""
import collections
import json
from pathlib import Path
import re
import sys
import unittest

from .nhart_test_resources import open_image, read_resource, verified_index

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from import_new_horizons_spell_art import parse_jsonc

RANKS = ("basic", "advanced", "expert")
SPELL_ROLES = ("iconBook", "iconScroll", "iconEffect", "iconImmune", "iconScenarioBonus")
PERK_ROW = re.compile(r'\{"(new-horizons:[^"]+)", "([^"]+)"\}')
NAMED_PERK_ROW = re.compile(r'\{"([^"]+)", "([^"]+)", "([^"]+)"\}')
ORIGINAL_SKILL = re.compile(r"(SECSK32|SECSKILL|SECSK82):0:(\d+)")
ORIGINAL_SPELL = re.compile(r"(SPELLS|SPELLSCR|SPELLINT|SPELLBON)\.def:0:(\d+)", re.IGNORECASE)


def load(path):
    return json.loads((ROOT / path).read_text(encoding="utf-8"))


def merge(target, patch):
    for key, value in patch.items():
        if isinstance(value, dict) and isinstance(target.get(key), dict):
            merge(target[key], value)
        else:
            target[key] = value


def spell_definitions(module):
    definitions = {}
    for path in sorted((ROOT / "config/spells").glob("*.json")):
        for key, value in parse_jsonc(path.read_text()).items():
            identity = key if ":" in key else "core:" + key
            merge(definitions.setdefault(identity, {}), value)
    for relative in module["spells"]:
        path = ROOT / "Mods/new-horizons/Content" / relative
        for key, value in parse_jsonc(path.read_text()).items():
            identity = key if ":" in key else "new-horizons:" + key
            merge(definitions.setdefault(identity, {}), value)
    return definitions


class LiveArtworkCompletenessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.skills = load("config/newHorizonsSkills.json")
        cls.perks = load("config/newHorizonsPerks.json")
        cls.magic = load("config/newHorizonsMagic.json")
        cls.module = load("Mods/new-horizons/mod.json")
        cls.selected = {key.casefold(): key for key in verified_index()}
        cls.definitions = spell_definitions(cls.module)

    def assert_selected(self, path):
        resource = ("SPRITES/" + path).casefold()
        self.assertTrue(resource in self.selected, "Missing selected artwork " + path)
        return self.selected[resource]

    def test_all_active_skills_have_distinct_rank_art_in_each_registered_role(self):
        for identity, skill in self.skills.items():
            for rank in RANKS:
                self.assertEqual(self.module["skills"][identity][rank]["images"], skill[rank]["images"])
            roles = set(skill["basic"]["images"])
            for rank in RANKS:
                self.assertEqual(set(skill[rank]["images"]), roles)
            for role in roles:
                identities = []
                pixels = []
                for rank in RANKS:
                    binding = skill[rank]["images"][role]
                    with self.subTest(skill=identity, rank=rank, role=role):
                        self.assertNotRegex(binding.lower(), r"neutral|placeholder|fallback")
                        original = ORIGINAL_SKILL.fullmatch(binding)
                        if original:
                            identities.append((original[1], int(original[2])))
                        else:
                            resource = self.assert_selected(binding)
                            identities.append(resource)
                            pixels.append(open_image(resource).convert("RGBA").tobytes())
                self.assertEqual(len(set(identities)), len(RANKS), identity + " shares rank bindings")
                if pixels:
                    self.assertEqual(len(set(pixels)), len(RANKS), identity + " shares rank image bytes")

    def test_every_current_perk_resolves_one_selected_nonplaceholder_descriptor(self):
        source = (ROOT / "client/windows/NewHorizonsPerkIcons.h").read_text()
        rows = PERK_ROW.findall(source)
        bindings = dict(rows)
        counts = collections.Counter(identity for identity, _ in rows)
        hybrid = (ROOT / "client/windows/NewHorizonsSylvanHybridPerkIcons.h").read_text()
        named = {(identity, name): stem for identity, name, stem in NAMED_PERK_ROW.findall(hybrid)}
        for skill in self.perks["skills"].values():
            for perk in skill["perks"]:
                identity = perk["id"]
                with self.subTest(perk=identity):
                    self.assertEqual(counts[identity], 1, "Missing or duplicate default perk binding")
                    stem = bindings.get(identity, "NH_perk_neutral")
                    if identity.startswith("new-horizons:sylvanLuck.") and self.perks["sylvanTacticsPolicy"] == 3:
                        stem = named.get((identity, perk["name"]), "NH_perk_neutral")
                    if identity == "new-horizons:sorceryMagic.countermage" and perk["name"] in ("Arcane Ballistics", "Arcane Arsenal"):
                        stem = "NH_perk_arcane_ballistics"
                    self.assertNotRegex(stem.lower(), r"neutral|placeholder|fallback")
                    descriptor = json.loads(read_resource(self.assert_selected(stem + ".json")))
                    for frame in descriptor["images"]:
                        if "file" in frame:
                            self.assert_selected(frame["file"])

    def test_every_active_hero_spell_has_its_own_selected_or_canonical_original_art(self):
        for identity, policy in self.magic["spells"].items():
            if not policy.get("active", True) or not policy.get("heroAccess", True):
                continue
            definition = self.definitions.get(identity, {})
            graphics = definition.get("graphics", {})
            for role in SPELL_ROLES:
                with self.subTest(spell=identity, role=role):
                    binding = graphics.get(role)
                    if identity.startswith("new-horizons:"):
                        self.assertTrue(binding, "Custom spell has no explicit " + role)
                        self.assertNotRegex(binding.lower(), r"neutral|placeholder|fallback")
                        self.assert_selected(binding)
                    elif binding:
                        if binding.startswith("NH_"):
                            self.assert_selected(binding)
                        else:
                            match = ORIGINAL_SPELL.fullmatch(binding)
                            self.assertIsNotNone(match, "Unknown original icon syntax")
                            expected = definition.get("index")
                            self.assertIsInstance(expected, int)
                            offset = 1 if role in ("iconEffect", "iconImmune") else 0
                            self.assertEqual(int(match[2]), expected + offset,
                                             "A core spell borrows another spell's icon")
                    else:
                        self.assertIsInstance(definition.get("index"), int,
                                              "No canonical original spell frame")

    def test_explicit_nonhero_spell_art_resolves_without_placeholders(self):
        for identity, definition in self.definitions.items():
            policy = self.magic["spells"].get(identity, {})
            if not policy.get("active", True) or policy.get("heroAccess", True):
                continue
            for role, binding in definition.get("graphics", {}).items():
                if role not in SPELL_ROLES:
                    continue
                with self.subTest(spell=identity, role=role):
                    self.assertNotRegex(binding.lower(), r"neutral|placeholder|fallback")
                    if binding.startswith("NH_"):
                        self.assert_selected(binding)

    def test_shared_custom_spellbook_icons_are_only_variants_of_the_same_base_spell(self):
        shared = collections.defaultdict(list)
        for identity, policy in self.magic["spells"].items():
            if not policy.get("active", True) or not policy.get("heroAccess", True):
                continue
            binding = self.definitions.get(identity, {}).get("graphics", {}).get("iconBook")
            if binding:
                shared[binding].append(identity)
        for binding, identities in shared.items():
            if len(identities) < 2:
                continue
            with self.subTest(binding=binding):
                bases = {self.magic["spells"][identity].get("variant", {}).get("base", identity)
                         for identity in identities}
                self.assertEqual(len(bases), 1, "Unrelated spells share one painting")
