"""Mutation controls for exact released-art lineage; no private files or Git required."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest
from .nhart_source_transitions import read_historical_source

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from import_new_horizons_art_wave import entries_digest
from prepare_new_horizons_authority_art import binding_block
from .nhart_released_history import (authority_before_leadership_rework,
    released_predecessor_entries, baseline_payload_proof, WISP_REPLACEMENTS)
from .nhart_sylvan_alliance_presentations import validate_alliance_resource_reuse
from .nhart_test_resources import read_resource
from .nhart_skill_rank_art import skill_rank_wave
from .nhart_release_presentation import presentation_wave
from .nhart_living_armor_backdrop import backdrop_wave
from .nhart_creature_ability_art import ability_wave
from .nhart_interface_status_art import interface_status_wave
from .nhart_academy_town_animation import town_animation_wave
from .nhart_retired_interface_icons import retirement_wave, retired_count
from .nhart_current_faction_perk_art import current_faction_perk_wave_from


class ReleasedArtHistoryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.entries = json.loads((ROOT / 'assets/new-horizons/runtime-art-manifest.json').read_text())['entries']
        cls.waves = json.loads((ROOT / 'assets/new-horizons/runtime-art-additions.json').read_text())['waves']
        cls.header = read_historical_source('client/windows/NewHorizonsPerkIcons.h')

    def test_exact_authority_reversal_and_duplicate_or_mixed_blocks(self):
        original = binding_block()
        previous = authority_before_leadership_rework(self.header, original)
        self.assertEqual(hashlib.sha256(previous.encode()).hexdigest(),
                         '34d3250f83797b21337d74004e1f5514696efde2e0776ec10664ae02d8bef505')
        self.assertEqual(authority_before_leadership_rework(previous, original), previous)
        for changed in (self.header + original, previous + original,
                        self.header.replace('authority.massedRanks', 'authority.unknown'),
                        self.header.replace('NH_perk_authority_offensive_drills', 'NH_wrong', 1),
                        self.header + '\t\t{"new-horizons:authority.massedRanks", "NH_perk_authority_offensive_drills"},\n'):
            with self.assertRaises(ValueError): authority_before_leadership_rework(changed, original)
        unrelated = self.header.replace('NH_perk_neutral', 'NH_changed_neutral')
        self.assertEqual(authority_before_leadership_rework(unrelated, original),
                         previous.replace('NH_perk_neutral', 'NH_changed_neutral'))

    def test_bridge_requires_exact_entries_provenance_and_archive_boundary(self):
        previous, index = released_predecessor_entries(self.entries, self.waves, entries_digest)
        current_rank_wave = skill_rank_wave(self.waves)
        self.assertEqual(len(current_rank_wave['resources']), 132)
        current_perk_wave = current_faction_perk_wave_from(self.waves)
        self.assertEqual((len(previous), index),
                         (5859 + len(current_rank_wave['resources']) + len(current_perk_wave['resources'])
                          + len(presentation_wave(self.waves)['resources'])
                          + len(backdrop_wave(self.waves)['resources'])
                          + len(ability_wave(self.waves)['resources'])
                          + len(interface_status_wave(self.waves)['resources'])
                          + len(town_animation_wave(self.waves)['resources'])
                          - retired_count(self.waves), 41))
        for family in ('mummy-runtime', 'conflux-water-ice-runtime', 'wisps-and-projectiles'):
            changed = copy.deepcopy(self.entries)
            next(row for row in changed if row['family'] == family)['origin'] = 'unreviewed'
            with self.assertRaises(ValueError): released_predecessor_entries(changed, self.waves, entries_digest)
        changed = copy.deepcopy(self.entries); changed.pop()
        with self.assertRaises(ValueError): released_predecessor_entries(changed, self.waves, entries_digest)
        for wave_index, field in ((40, 'newArchiveSha256'), (41, 'baselineArchiveSha256'), (41, 'baselineCount')):
            waves = copy.deepcopy(self.waves); waves[wave_index][field] = 0
            with self.assertRaises(ValueError): released_predecessor_entries(self.entries, waves, entries_digest)

    def test_current_rank_art_descendant_requires_its_exact_selected_wave(self):
        selected = skill_rank_wave(self.waves)
        index = self.waves.index(selected)
        for field in ('resources', 'baselineEntriesSha256', 'newArchiveSha256'):
            changed = copy.deepcopy(self.waves)
            changed[index][field] = 'changed'
            with self.subTest(field=field), self.assertRaises(ValueError):
                released_predecessor_entries(self.entries, changed, entries_digest)
        for changed in ([wave for wave in self.waves if wave is not selected],
                        self.waves + [selected]):
            with self.assertRaises(ValueError):
                released_predecessor_entries(self.entries, changed, entries_digest)

    def test_wisp_replacement_proof_is_typed_and_never_returns_old_pixels(self):
        previous, _ = released_predecessor_entries(self.entries, self.waves, entries_digest)
        before = {row['resource']: row for row in previous}
        current = {row['resource']: row for row in self.entries}
        for name in WISP_REPLACEMENTS:
            payload = read_resource(name)
            self.assertEqual(baseline_payload_proof(before[name], payload), 'verified-released-replacement')
            self.assertEqual(baseline_payload_proof(current[name], payload), 'verified-matching-payload')
            with self.assertRaises(ValueError): baseline_payload_proof(before[name], payload + b'bad')
            changed = dict(before[name], approval='unreviewed')
            with self.assertRaises(ValueError): baseline_payload_proof(changed, payload)
        ordinary = next(row for row in previous if row['resource'] not in WISP_REPLACEMENTS)
        self.assertEqual(baseline_payload_proof(ordinary, read_resource(ordinary['resource'])), 'verified-matching-payload')
        with self.assertRaises(ValueError): baseline_payload_proof(ordinary, b'bad')

    def test_new_wave_has_exact_additions_and_existing_reuses(self):
        wave = next(wave for wave in self.waves if wave['baselineArchiveSha256'] ==
                    '37028037a0086b1f6f1937af9c2f21a08e6513ab47c83293af6e0386c1549171')
        declared = {name for row in wave['perkPresentations'] for name in row['resources']}
        validate_alliance_resource_reuse(wave, declared, self.entries, self.waves, 41, read_resource)
        with self.assertRaises(ValueError):
            validate_alliance_resource_reuse(wave, declared | {'SPRITES/extra.png'}, self.entries, self.waves, 41, read_resource)
        changed = copy.deepcopy(wave); changed['resources'].append('SPRITES/NH_perk_sylvan_tactics_wild_hunt.png')
        with self.assertRaises(ValueError):
            validate_alliance_resource_reuse(changed, declared, self.entries, self.waves, 41, read_resource)
        missing = [row for row in self.entries if row['resource'] != 'SPRITES/NH_perk_sylvan_tactics_wild_hunt.png']
        with self.assertRaises(ValueError):
            validate_alliance_resource_reuse(wave, declared, missing, self.waves, 41, read_resource)
        with self.assertRaises(ValueError):
            validate_alliance_resource_reuse(wave, declared, self.entries, self.waves, 41, lambda _: b'changed')


if __name__ == '__main__':
    unittest.main()
