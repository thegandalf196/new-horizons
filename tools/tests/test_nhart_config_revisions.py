"""Exact accepted config-lineage reconstruction and fail-closed current checks."""
import copy
import json
import sys
from pathlib import Path
import unittest
from .nhart_config_revisions import config_at_revision, wave_config_revision

ROOT = Path(__file__).resolve().parents[2]
CONFIG = 'Mods/new-horizons/Content/config/spells/newHorizons.json'
MASS = 'Mods/new-horizons/Content/config/spells/massVariants.json'


class ArtConfigRevisionTest(unittest.TestCase):
    def setUp(self):
        sys.path.insert(0, str(ROOT / 'tools'))
        from import_new_horizons_spell_art import parse_jsonc
        self.proofs = json.loads((ROOT / 'assets/new-horizons/runtime-art-config-revisions.json').read_text())
        self.configs = {path: parse_jsonc((ROOT / path).read_text()) for path in (CONFIG, MASS)}

    def test_exact_current_and_historical_stage_fields_without_input_mutation(self):
        original = copy.deepcopy(self.configs)
        old = config_at_revision(self.configs[CONFIG], CONFIG, self.proofs, 0)
        self.assertNotIn('theChosen', old)
        self.assertEqual(old['focusMagic']['level'], 3)
        self.assertEqual(old['focusMagic']['levels']['basic']['cost'], 11)
        self.assertIn('theChosen', config_at_revision(self.configs[CONFIG], CONFIG, self.proofs, 4))
        self.assertEqual(config_at_revision(self.configs[CONFIG], CONFIG, self.proofs, 5), self.configs[CONFIG])
        mass = config_at_revision(self.configs[MASS], MASS, self.proofs, 0)
        self.assertEqual(mass['massSlow']['level'], 1)
        self.assertEqual(mass['massSlow']['levels']['basic']['cost'], 12)
        self.assertEqual(mass['massSlow']['levels']['expert']['cost'], 9)
        self.assertEqual(config_at_revision(self.configs[MASS], MASS, self.proofs, 1), self.configs[MASS])
        self.assertEqual(self.configs, original)

    def test_future_nongraphics_keys_values_and_descriptions_fail_current_projection(self):
        for path in (CONFIG, MASS):
            for mutation in ('unknown', 'cost', 'description'):
                changed = copy.deepcopy(self.configs[path])
                key = 'focusMagic' if path == CONFIG else 'massSlow'
                if mutation == 'unknown': changed['unreviewedNewSpell'] = {'level':1}
                elif mutation == 'cost': changed[key]['levels']['basic']['cost'] += 1
                else: changed[key]['levels']['basic']['description'] += ' unreviewed'
                with self.subTest(path=path, mutation=mutation), self.assertRaises(ValueError):
                    config_at_revision(changed, path, self.proofs, 0)

    def test_only_exact_graphics_pointers_are_left_to_existing_art_checks(self):
        changed = copy.deepcopy(self.configs[CONFIG])
        changed['arcaneBreach']['graphics']['iconEffect'] = 'different-art.png'
        old = config_at_revision(changed, CONFIG, self.proofs, 0)
        self.assertEqual(old['arcaneBreach']['graphics']['iconEffect'], 'different-art.png')
        changed['arcaneBreach']['graphics']['unreviewedRole'] = 'unreviewed.png'
        with self.assertRaises(ValueError): config_at_revision(changed, CONFIG, self.proofs, 0)
        changed = copy.deepcopy(self.configs[CONFIG])
        del changed['arcaneBreach']['graphics']['iconEffect']
        with self.assertRaises(ValueError): config_at_revision(changed, CONFIG, self.proofs, 0)

    def test_revision_metadata_unknown_fields_paths_commits_and_hashes_cannot_be_renewed_silently(self):
        for field in ('sourceCommit', 'fromProjectionSha256', 'toProjectionSha256', 'changes'):
            changed = copy.deepcopy(self.proofs)
            row = changed['files'][CONFIG]['revisions'][0]
            row[field] = [] if field == 'changes' else 'f'*64
            with self.subTest(field=field), self.assertRaises(ValueError):
                config_at_revision(self.configs[CONFIG], CONFIG, changed, 0)
        changed = copy.deepcopy(self.proofs)
        changed['files'][CONFIG]['excludedGraphics'].append('/focusMagic/level')
        with self.assertRaises(ValueError): config_at_revision(self.configs[CONFIG], CONFIG, changed, 0)
        changed = copy.deepcopy(self.proofs); changed['extra'] = True
        with self.assertRaises(ValueError): config_at_revision(self.configs[CONFIG], CONFIG, changed, 0)
        for revision in (True, -1, 6, '0'):
            with self.assertRaises(ValueError): config_at_revision(self.configs[CONFIG], CONFIG, self.proofs, revision)
        with self.assertRaises(ValueError): config_at_revision({}, 'unknown.json', self.proofs, 0)

    def test_old_wave_defaults_and_exact_new_wave_selection(self):
        self.assertEqual(wave_config_revision({}, CONFIG), 0)
        selected = {'configRevisions':{CONFIG:5, MASS:1}}
        self.assertEqual(wave_config_revision(selected, CONFIG), 5)
        self.assertEqual(wave_config_revision(selected, MASS), 1)
        for record in ({'unknown.json':1}, {CONFIG:True}, {CONFIG:-1}):
            with self.assertRaises(ValueError): wave_config_revision({'configRevisions':record}, CONFIG)
