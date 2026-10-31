"""Actual current-original cohort and bounded provenance corruption controls."""
import copy
import hashlib
import json
from pathlib import Path
import unittest
from .nhart_legacy_art_lineage import APPROVED_SPECIALTIES, _digest, legacy_specialty_family
from .nhart_specialty_lineage import CURRENT_ORIGINAL_COHORT, CURRENT_ORIGINAL_SPECIALTIES, specialty_family
from .nhart_test_resources import read_resource

ROOT = Path(__file__).resolve().parents[2]


class CurrentOriginalSpecialtyLineageTest(unittest.TestCase):
    def setUp(self):
        self.waves = json.loads((ROOT / 'assets/new-horizons/runtime-art-additions.json').read_text())['waves']
        self.manifest = {row['resource']: row for row in json.loads(
            (ROOT / 'assets/new-horizons/runtime-art-manifest.json').read_text())['entries']}
        self.currentRecords = [row for wave in self.waves for row in wave.get('specialties', [])
                               if 'provenanceCohort' in row]
        self.records = [row for wave in self.waves for row in wave.get('specialties', [])
                        if row.get('provenanceCohort') == CURRENT_ORIGINAL_COHORT]

    def test_exact_six_originals_and_eleven_historical_records_remain_distinct(self):
        self.assertEqual(len(self.records), 6)
        self.assertEqual({row['id'] for row in self.records}, set(CURRENT_ORIGINAL_SPECIALTIES))
        historical = [row for wave in self.waves for row in wave.get('specialties', [])
                      if 'sourceEntries' in row and 'provenanceCohort' not in row]
        self.assertEqual(len(historical), 11)
        self.assertEqual({row['id'] for row in historical}, set(APPROVED_SPECIALTIES))
        for row in historical:
            self.assertEqual(_digest(row), APPROVED_SPECIALTIES[row['id']])
            self.assertEqual(specialty_family(row, self.manifest), row['family'])
        for row in self.records:
            self.assertEqual(row['provenanceCohort'], CURRENT_ORIGINAL_COHORT)
            self.assertEqual(specialty_family(row, self.manifest), 'hero-specialty-icons')
            with self.assertRaises(ValueError):
                legacy_specialty_family(row, self.manifest)
            for entry in row['sourceEntries']:
                self.assertEqual(hashlib.sha256(read_resource(entry['resource'])).hexdigest(), entry['sha256'])

    def test_every_record_field_is_pinned_and_no_cohort_escape_is_accepted(self):
        for row in self.currentRecords:
            for field in row:
                changed = copy.deepcopy(row)
                changed[field] = 'changed'
                with self.subTest(id=row['id'], field=field), self.assertRaises(ValueError):
                    specialty_family(changed, self.manifest)
            for field in ('provenanceCohort', 'sourceEntries', 'family'):
                changed = copy.deepcopy(row)
                del changed[field]
                with self.subTest(id=row['id'], missing=field), self.assertRaises(ValueError):
                    specialty_family(changed, self.manifest)
            changed = copy.deepcopy(row)
            changed['id'] = 'core:unreviewedFutureHero'
            with self.assertRaises(ValueError):
                specialty_family(changed, self.manifest)

    def test_manifest_provenance_mutation_and_historical_relabeling_fail(self):
        for row in self.currentRecords:
            for entry in row['sourceEntries']:
                for field in ('origin', 'sha256', 'approval', 'selection', 'family'):
                    changed = copy.deepcopy(self.manifest)
                    changed[entry['resource']][field] = 'changed'
                    with self.subTest(resource=entry['resource'], field=field), self.assertRaises(ValueError):
                        specialty_family(row, changed)
        historical = next(row for wave in self.waves for row in wave.get('specialties', [])
                          if 'sourceEntries' in row and 'provenanceCohort' not in row)
        changed = copy.deepcopy(historical)
        changed['provenanceCohort'] = CURRENT_ORIGINAL_COHORT
        with self.assertRaises(ValueError):
            specialty_family(changed, self.manifest)

    def test_exact_append_wave_keeps_baseline_and_selection_proof(self):
        waves = [wave for wave in self.waves if any(
            row.get('provenanceCohort') == CURRENT_ORIGINAL_COHORT
            for row in wave.get('specialties', []))]
        self.assertEqual(len(waves), 1)
        wave = waves[0]
        self.assertEqual(_digest(wave), 'b0074598944a562bfe971b3c7647873b739264df132576951ec1831b2253895e')
        self.assertEqual(wave['baselineCount'], 5511)
        self.assertEqual(wave['entryCount'], 5535)
        self.assertEqual(len(wave['resources']), 24)
        self.assertEqual(len(set(wave['resources'])), 24)
        self.assertEqual(set(wave['resources']), {name for row in self.records for name in row['resources']})
