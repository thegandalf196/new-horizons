"""Exact thirteen-record append, independent from earlier specialty cohorts."""
import copy
import json
from pathlib import Path
import re
import unittest
from .nhart_legacy_art_lineage import _digest
from .nhart_specialty_lineage import (CURRENT_ORIGINAL_COHORT, CURRENT_THIRTEEN_COHORT,
                                      CURRENT_THIRTEEN_SPECIALTIES, specialty_family)

ROOT = Path(__file__).resolve().parents[2]


def ordinary_registry(root):
    """Return the capture/dispatch source and sole table across reviewed layouts."""
    owners = ('NewHorizonsAdditionalSpecialties.cpp', 'NewHorizonsOrdinaryCreatureSpecialties.cpp',
              'OrdinaryCreatureSpecialtyRegistry.cpp')
    registries = []
    for owner in owners:
        path = root / 'lib/entities/hero' / owner
        if path.exists():
            source = path.read_text()
            for match in re.finditer(r'const std::array<OrdinaryCreatureSpecialty,\s*\d+>\s+'
                                     r'ordinaryCreatureSpecialties\s*\{\{(.*?)\}\};', source, re.S):
                dispatch_source = source
                if owner == 'OrdinaryCreatureSpecialtyRegistry.cpp':
                    dispatch_source = (root / 'lib/entities/hero' /
                                       'NewHorizonsOrdinaryCreatureSpecialties.cpp').read_text()
                registries.append((dispatch_source, match.group(1)))
    if len(registries) != 1:
        raise ValueError('Expected exactly one ordinary specialty registry')
    return registries[0]


def registry_artwork(table):
    """Bind art only from each registry row's terminal optional artwork field."""
    bindings = {}
    for hero, body in re.findall(r'\{\s*"(core:[^"\n]+)",([^{}]*)\}', table):
        if hero in bindings:
            raise ValueError('Duplicate ordinary specialty identity')
        artwork = re.search(r',\s*"(newHorizons/heroSpecialties/[^"\n]+)"\s*$', body)
        bindings[hero] = artwork.group(1) if artwork else None
    return bindings


class ThirteenSpecialtyLineageTest(unittest.TestCase):
    def setUp(self):
        self.waves = json.loads((ROOT / 'assets/new-horizons/runtime-art-additions.json').read_text())['waves']
        self.wave = self.waves[32]
        self.rows = self.wave['specialties']
        self.manifest = {row['resource']: row for row in json.loads(
            (ROOT / 'assets/new-horizons/runtime-art-manifest.json').read_text())['entries']}

    def test_exact_separate_cohort_and_append_preserve_previous_waves(self):
        self.assertEqual(len(self.rows), 13)
        self.assertEqual({row['id'] for row in self.rows}, set(CURRENT_THIRTEEN_SPECIALTIES))
        self.assertEqual(_digest(self.waves[:32]), 'fce0feef49032c1cc6eddaf4761843334f7a3b72894c94175946bfe99573b9a8')
        self.assertEqual(_digest(self.wave), 'fe69317db0b1eae6174d017d832d8121169fac617702051531ec1e6a65beb1df')
        self.assertEqual((self.wave['baselineCount'], self.wave['entryCount']), (5535, 5587))
        resources = [name for row in self.rows for name in row['resources']]
        self.assertEqual(len(resources), 52)
        self.assertEqual(len(set(resources)), 52)
        self.assertEqual(set(resources), set(self.wave['resources']))
        for row in self.rows:
            self.assertEqual(row['provenanceCohort'], CURRENT_THIRTEEN_COHORT)
            self.assertEqual(specialty_family(row, self.manifest), 'hero-specialty-icons')
            for cohort in (CURRENT_ORIGINAL_COHORT, 'unknown'):
                changed = copy.deepcopy(row)
                changed['provenanceCohort'] = cohort
                with self.assertRaises(ValueError):
                    specialty_family(changed, self.manifest)

    def test_current_registry_retains_exact_selected_bindings_and_charna_fix(self):
        # Source-only thirteen-value growth was reviewed at adoption. Its original
        # baseline fingerprint remains historical evidence, not a file-location lock.
        self.assertEqual(self.wave['baselineSpecialtyPresentationSourceSha256'],
                         'ea3c71f42be65a7b538683bcc69ac86dd78f727d16ad5ed834ecda49c90e42a3')
        source, table = ordinary_registry(ROOT)
        bindings = registry_artwork(table)
        earlier = [row for wave in self.waves for row in wave.get('specialties', [])
                   if row.get('provenanceCohort') == CURRENT_ORIGINAL_COHORT]
        self.assertEqual(len(earlier), 6)
        for row in earlier + self.rows:
            self.assertEqual(bindings[row['id']], 'newHorizons/heroSpecialties/' + row['identity'])
        self.assertIn('BonusNodeType::BATTLE_WIDE', source)

    def test_registry_contract_rejects_missing_duplicate_wrong_and_nonterminal_art(self):
        _, table = ordinary_registry(ROOT)
        row = self.rows[0]
        hero = row['id']
        art = 'newHorizons/heroSpecialties/' + row['identity']
        self.assertEqual(registry_artwork(table)[hero], art)
        for changed in (table.replace(art, 'newHorizons/heroSpecialties/WrongIdentity'),
                        table.replace('"' + art + '"', 'std::nullopt'),
                        table.replace('"' + art + '"', '"' + art + '", "ignored"')):
            self.assertNotEqual(registry_artwork(changed).get(hero), art)
        duplicate = re.search(r'\{\s*"' + re.escape(hero) + r'",[^{}]*\}', table).group()
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            registry_artwork(table + duplicate)
