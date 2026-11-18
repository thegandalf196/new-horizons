"""Current consumers remain explicit while approved historical bytes stay pinned."""
import copy
import csv
import hashlib
import json
from pathlib import Path
import re
import sys
import unittest
from unittest import mock

from .nhart_source_transitions import source_records, historical_source, read_historical_source
from .nhart_sorcery_config_transition import CONFIG, before_sorcery, transition_record
from .nhart_current_faction_perk_art import BINDINGS

ROOT = Path(__file__).resolve().parents[2]


class ArtSourceTransitionTest(unittest.TestCase):
    def test_exact_sources_reverse_without_discarding_unrelated_bytes(self):
        for path, record in source_records().items():
            source = (ROOT / path).read_text()
            old = read_historical_source(path)
            self.assertEqual(hashlib.sha256(old.encode()).hexdigest(), record['historicalSha256'])
            self.assertEqual(historical_source(path, source + '\n// unrelated witness\n'),
                             old + '\n// unrelated witness\n')
            stage = source
            for index in reversed(range(len(record['changes']))):
                change = record['changes'][index]
                self.assertEqual(stage.count(change['after']), 1)
                remaining = {**record, 'changes': record['changes'][:index + 1]}
                with mock.patch('tools.tests.nhart_source_transitions.source_records',
                                return_value={path: remaining}):
                    with self.subTest(path=path, stage=index), self.assertRaises(ValueError):
                        historical_source(path, stage.replace(change['after'], '', 1))
                    with self.assertRaises(ValueError):
                        historical_source(path, stage + change['after'])
                stage = stage.replace(change['after'], change['before'], 1)
            self.assertEqual(stage, old)

    def test_current_metamagic_and_estates_bindings_preserve_selected_historical_art(self):
        text = (ROOT / 'client/windows/NewHorizonsPerkIcons.h').read_text()
        bindings = dict(re.findall(r'\{"(new-horizons:[^"]+)", "([^"]+)"\}', text))
        roster = json.loads((ROOT / 'config/newHorizonsPerks.json').read_text())
        ids = {row['id'] for row in roster['skills']['new-horizons:metamagic']['perks']}
        self.assertEqual(len(ids), 10)
        self.assertEqual({identity for identity in bindings if identity.startswith('new-horizons:metamagic.')}, ids)
        self.assertEqual({identity: bindings[identity] for identity in ids},
                         {identity: BINDINGS[identity] for identity in ids})
        with (ROOT / 'docs/NH_UI_ASSET_INVENTORY.csv').open() as stream:
            inventory = list(csv.DictReader(stream))
        for identity in ids:
            rows = [row for row in inventory if row['Item / ID'].endswith('(' + identity + ')')]
            self.assertEqual(len(rows), 1)
            self.assertEqual(rows[0]['Art'], 'Provisional')
        for identity, icon in (('alchemist', 'prospector'), ('hoarder', 'financier'),
                               ('decentralization', 'magnate'), ('supervisor', 'land_surveyor')):
            self.assertEqual(bindings['new-horizons:estates.' + identity], 'NH_perk_' + icon)
        halon = (ROOT / 'lib/entities/hero/NewHorizonsSpecialtyArtwork.h').read_text()
        self.assertIn('{"core:halon", SpecialtyArtworkFamily::Identity, "core:halon", "HalonMetamagicAdept"}', halon)
        captured = (ROOT / 'client/windows/NewHorizonsCapturedPerkIcons.h').read_text()
        self.assertIn('!newHorizonsEstates::usesRevisedPolicy(capturedRules)', captured)

    def test_consecrated_arms_roster_preserves_historical_divine_bindings(self):
        path = 'client/windows/NewHorizonsPerkIcons.h'
        current = (ROOT / path).read_text()
        pattern = r'\{"(new-horizons:[^"]+)", "([^"]+)"\}'
        bindings = dict(re.findall(pattern, current))
        historical = dict(re.findall(pattern, read_historical_source(path)))
        roster = json.loads((ROOT / 'config/newHorizonsPerks.json').read_text())
        ids = {row['id'] for row in roster['skills']['new-horizons:divineMandate']['perks']}
        self.assertEqual(len(ids), 10)
        self.assertEqual({identity: bindings[identity] for identity in ids},
                         {identity: BINDINGS[identity] for identity in ids})
        old = {identity: icon for identity, icon in historical.items()
               if identity.startswith('new-horizons:divineMandate.')}
        self.assertTrue(old)
        self.assertFalse(ids.intersection(old))
        self.assertEqual({identity: icon for identity, icon in bindings.items()
                          if identity.startswith('new-horizons:divineMandate.')},
                         {**old, **{identity: BINDINGS[identity] for identity in ids}})
        with (ROOT / 'docs/NH_UI_ASSET_INVENTORY.csv').open() as stream:
            inventory = list(csv.DictReader(stream))
        for identity in ids:
            rows = [row for row in inventory if row['Item / ID'].endswith('(' + identity + ')')]
            self.assertEqual(len(rows), 1)
            self.assertEqual(rows[0]['Area'], 'Active perk')
            self.assertEqual(rows[0]['Art'], 'Provisional')
            self.assertIn(BINDINGS[identity], rows[0]['Live binding / source'])
        for row in inventory:
            if any(row['Item / ID'].endswith('(' + identity + ')') for identity in old):
                self.assertNotEqual(row['Area'], 'Active perk')

    def test_transition_records_cannot_silently_change(self):
        with mock.patch.object(Path, 'read_bytes', return_value=b'{"unreviewed":true}'):
            with self.assertRaises(ValueError):
                source_records()
            with self.assertRaises(ValueError):
                transition_record()


class SorceryArtConfigTransitionTest(unittest.TestCase):
    def setUp(self):
        sys.path.insert(0, str(ROOT / 'tools'))
        from import_new_horizons_spell_art import parse_jsonc
        self.config = parse_jsonc((ROOT / CONFIG).read_text())

    def test_restores_only_recorded_history_and_preserves_selected_status_graphics(self):
        original = copy.deepcopy(self.config)
        old = before_sorcery(self.config)
        self.assertNotIn('arcaneBreach', self.config)
        self.assertIn('arcaneBreach', old)
        self.assertNotIn('forceLance', old)
        for row in transition_record()['changes']:
            if 'before' in row:
                self.assertEqual(old[row['key']], row['before'])
        self.assertEqual(self.config, original)
        self.assertEqual(before_sorcery(old), old)

    def test_unrecorded_spell_removal_value_or_graphics_role_is_rejected(self):
        for mutation in ('unknown', 'removed', 'cost', 'role', 'retired'):
            changed = copy.deepcopy(self.config)
            if mutation == 'unknown':
                changed['unknownSpell'] = {}
            elif mutation == 'removed':
                del changed['forceLance']
            elif mutation == 'cost':
                changed['focusMagic']['levels']['basic']['cost'] += 1
            elif mutation == 'role':
                changed['lucidity']['graphics']['unknownRole'] = 'unknown.png'
            else:
                changed['arcaneBreach'] = before_sorcery(self.config)['arcaneBreach']
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                before_sorcery(changed)
