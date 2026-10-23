"""Reversible descriptor history keeps old payload proofs meaningful."""
import copy
import hashlib
import json
import unittest

from .nhart_test_resources import entries_before_wave, resource_before_wave


def encoded(descriptor):
    return (json.dumps(descriptor, indent=2) + '\n').encode()


def entry(payload, name='SPRITES/unit.json'):
    return {'resource': name, 'size': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()}


class ArtHistoryTest(unittest.TestCase):
    def setUp(self):
        self.old = encoded({'basepath': 'unit/', 'sequences': [{'group': 2, 'frames': ['standing.png']}]})
        self.added = {'group': 1, 'generateOverlay': 1, 'frames': ['fidget.png']}
        descriptor = json.loads(self.old)
        descriptor['sequences'].append(self.added)
        self.current = encoded(descriptor)
        self.replacement = {'previous': entry(self.old), 'current': entry(self.current),
                            'removeAppendedSequence': self.added}
        self.waves = [{'resources': ['SPRITES/fidget.png'], 'replacedEntries': [self.replacement]}]
        self.entries = [entry(self.current), entry(b'synthetic', 'SPRITES/fidget.png')]

    def test_reconstructs_exact_old_entry_and_payload_without_mutating_inputs(self):
        original = copy.deepcopy((self.entries, self.waves))
        self.assertEqual(entries_before_wave(self.entries, self.waves, 0), [entry(self.old)])
        self.assertEqual(resource_before_wave('SPRITES/unit.json', self.waves, 0,
                                             lambda _: self.current), self.old)
        self.assertEqual((self.entries, self.waves), original)
        self.assertEqual(resource_before_wave('SPRITES/unit.json', self.waves, 1,
                                             lambda _: self.current), self.current)

    def test_rejects_current_or_historical_payload_drift(self):
        for field in ('previous', 'current'):
            with self.subTest(field=field):
                waves = copy.deepcopy(self.waves)
                waves[0]['replacedEntries'][0][field]['sha256'] = '0' * 64
                with self.assertRaises(ValueError):
                    resource_before_wave('SPRITES/unit.json', waves, 0, lambda _: self.current)

    def test_rejects_wrong_sequence_and_unsupported_reversal(self):
        for change in ({'removeAppendedSequence': {'group': 1, 'frames': []}}, {'unknown': True}):
            with self.subTest(change=change):
                waves = copy.deepcopy(self.waves)
                waves[0]['replacedEntries'][0].update(change)
                with self.assertRaises(ValueError):
                    resource_before_wave('SPRITES/unit.json', waves, 0, lambda _: self.current)

    def test_rejects_broken_identity_and_added_replaced_overlap(self):
        for change in ('identity', 'overlap', 'duplicate'):
            with self.subTest(change=change):
                waves = copy.deepcopy(self.waves)
                if change == 'identity':
                    waves[0]['replacedEntries'][0]['previous']['resource'] = 'SPRITES/other.json'
                elif change == 'overlap':
                    waves[0]['resources'].append('SPRITES/unit.json')
                else:
                    waves[0]['replacedEntries'].append(copy.deepcopy(self.replacement))
                with self.assertRaises(ValueError):
                    entries_before_wave(self.entries, waves, 0)

    def test_unmodified_payload_remains_byte_exact(self):
        self.assertEqual(resource_before_wave('SPRITES/other.json', self.waves, 0,
                                             lambda _: b'unchanged'), b'unchanged')

    def test_two_wave_chain_restores_each_exact_revision(self):
        added = {'group': 3, 'frames': ['reaction.png']}
        descriptor = json.loads(self.current)
        descriptor['sequences'].append(added)
        latest = encoded(descriptor)
        replacement = {'previous': entry(self.current), 'current': entry(latest),
                       'removeAppendedSequence': added}
        waves = self.waves + [{'resources': [], 'replacedEntries': [replacement]}]
        entries = [entry(latest), self.entries[1]]
        self.assertEqual(entries_before_wave(entries, waves, 0), [entry(self.old)])
        self.assertEqual(entries_before_wave(entries, waves, 1), self.entries)
        for index, expected in ((0, self.old), (1, self.current), (2, latest)):
            self.assertEqual(resource_before_wave('SPRITES/unit.json', waves, index,
                                                 lambda _: latest), expected)
        waves[1]['replacedEntries'][0]['previous'] = entry(self.old)
        with self.assertRaises(ValueError):
            entries_before_wave(entries, waves, 0)
        with self.assertRaises(ValueError):
            resource_before_wave('SPRITES/unit.json', waves, 0, lambda _: latest)
