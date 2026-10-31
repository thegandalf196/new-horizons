"""Finite Wave34 next-four provenance and current registry binding contracts."""
import copy
import json
from pathlib import Path
import unittest
from .nhart_legacy_art_lineage import _digest
from .nhart_specialty_lineage import (CURRENT_ORIGINAL_COHORT, CURRENT_THIRTEEN_COHORT,
                                      CURRENT_FOUR_COHORT, CURRENT_FOUR_SPECIALTIES, specialty_family)
from .test_nhart_thirteen_specialty_lineage import ordinary_registry, registry_artwork

ROOT = Path(__file__).resolve().parents[2]


class FourSpecialtyLineageTest(unittest.TestCase):
    def setUp(self):
        self.waves = json.loads((ROOT / 'assets/new-horizons/runtime-art-additions.json').read_text())['waves']
        self.wave = self.waves[33]
        self.rows = self.wave['specialties']
        self.manifest = {row['resource']: row for row in json.loads(
            (ROOT / 'assets/new-horizons/runtime-art-manifest.json').read_text())['entries']}

    def test_exact_wave_and_four_records_preserve_every_previous_wave(self):
        self.assertEqual(_digest(self.waves[:33]), '425b817513a3ec34870d4d17aadce5d1a1b2c7b6c1a32a1f392d696c059a8b5b')
        self.assertEqual(_digest(self.wave), '6e5fbc95ac7e678db7cf1484f171ad2c8182096c6ca9ba60addced42ecc17b61')
        self.assertEqual((self.wave['baselineCount'], self.wave['entryCount']), (5587, 5603))
        self.assertEqual(len(self.rows), 4)
        self.assertEqual({row['id'] for row in self.rows}, set(CURRENT_FOUR_SPECIALTIES))
        resources = [name for row in self.rows for name in row['resources']]
        self.assertEqual(len(resources), 16)
        self.assertEqual(len(set(resources)), 16)
        self.assertEqual(set(resources), set(self.wave['resources']))
        for row in self.rows:
            self.assertEqual(row['provenanceCohort'], CURRENT_FOUR_COHORT)
            self.assertEqual(specialty_family(row, self.manifest), 'hero-specialty-icons')

    def test_current_registry_four_bindings_and_historical_source_evidence(self):
        # The review-time source hash records the additive adoption proof only;
        # current ownership/layout is checked by the extraction-aware registry contract.
        self.assertEqual(self.wave['baselineSpecialtyPresentationSourceSha256'], 'bc34881ca2be73efa88de178b49f6208615d343ea2e453dd1f3f67ab17a2f2f4')
        source, table = ordinary_registry(ROOT)
        bindings = registry_artwork(table)
        for row in self.rows:
            self.assertEqual(bindings[row['id']], 'newHorizons/heroSpecialties/' + row['identity'])
        self.assertIn('BonusNodeType::BATTLE_WIDE', source)

    def test_four_cohort_cannot_relabel_earlier_or_future_records(self):
        for row in self.rows:
            for cohort in (CURRENT_ORIGINAL_COHORT, CURRENT_THIRTEEN_COHORT, 'unreviewed-future'):
                changed = copy.deepcopy(row)
                changed['provenanceCohort'] = cohort
                with self.assertRaises(ValueError):
                    specialty_family(changed, self.manifest)
        for wave in self.waves[:33]:
            for row in wave.get('specialties', []):
                if 'sourceEntries' not in row:
                    continue
                changed = copy.deepcopy(row)
                changed['provenanceCohort'] = CURRENT_FOUR_COHORT
                with self.assertRaises(ValueError):
                    specialty_family(changed, self.manifest)
