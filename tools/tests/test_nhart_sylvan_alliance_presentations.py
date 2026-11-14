"""Schema-3 Sylvan artwork guards; structural evidence is separate from rendered acceptance."""
import copy
import hashlib
import json
from io import BytesIO
from pathlib import Path
import unittest
from PIL import Image, ImageEnhance

from .nhart_perk_presentations import validate_perk_presentation, validate_perk_payload
from .nhart_sylvan_perk_presentations import SYLVAN_CONDITIONS, SYLVAN_ADAPTER_SHA256
from .nhart_sylvan_alliance_presentations import (
    ALLIANCE_ADAPTER_SHA256, ALLIANCE_CONDITIONS, ALLIANCE_ORIGIN,
    ALLIANCE_SELECTION, ALLIANCE_APPROVAL, source_bound_alliance_icon, validate_alliance_family)

ROOT = Path(__file__).resolve().parents[2]


class SylvanAlliancePresentationTest(unittest.TestCase):
    def setUp(self):
        self.header = (ROOT / 'client/windows/NewHorizonsPerkIcons.h').read_text()
        self.old = (ROOT / 'client/windows/NewHorizonsCapturedPerkIcons.h').read_text()
        self.wrapper = (ROOT / 'client/windows/NewHorizonsSylvanAlliancePerkIcons.h').read_text()

    def row(self, condition):
        identity, name, stem, historical = condition
        return dict(schemaVersion=3, capturedPolicy={'sylvanTacticsPolicy': 2},
                    id=identity, capturedName=name, icon44=stem, historicalIcon44=historical,
                    alphaMaximum44=255, alphaMaximum32=255, native44Sha256='a'*64,
                    native32Sha256='b'*64, masterSha256='c'*64, exportManifestSha256='d'*64,
                    selectionEvidenceSha256='e'*64, resolverSourceSha256=ALLIANCE_ADAPTER_SHA256,
                    origin=ALLIANCE_ORIGIN, selection=ALLIANCE_SELECTION, approval=ALLIANCE_APPROVAL,
                    resources=['SPRITES/'+stem+s for s in ('.json', '.png', '_32.png', '_normal.png',
                                                           '_pressed.png', '_disabled.png', '_highlighted.png')])

    def resolve(self, condition, policy):
        return source_bound_alliance_icon(self.header, self.old, self.wrapper,
                                          condition[0], condition[1], policy)

    def test_ten_current_bindings_and_unchanged_historical_policies(self):
        self.assertEqual(hashlib.sha256(self.old.encode()).hexdigest(), SYLVAN_ADAPTER_SHA256)
        self.assertEqual(len(ALLIANCE_CONDITIONS), 10)
        for condition, previous in zip(ALLIANCE_CONDITIONS, SYLVAN_CONDITIONS):
            validate_perk_presentation(self.row(condition), self.header, self.old, self.wrapper)
            self.assertEqual(self.resolve(condition, {'sylvanTacticsPolicy': 2}), condition[2])
            self.assertEqual(self.resolve(condition, {}), condition[3])
            self.assertEqual(self.resolve(previous, {'sylvanTacticsPolicy': 1}), previous[2])
            self.assertEqual(self.resolve(previous, {'sylvanTacticsPolicy': 2}), previous[2])
            expected = previous[2] if condition[1] == 'Perfect Moment' else condition[3]
            self.assertEqual(self.resolve(condition, {'sylvanTacticsPolicy': 1}), expected)
        self.assertEqual(self.resolve(('new-horizons:archery.crossfire', 'Crossfire'),
                                      {'sylvanTacticsPolicy': 2}), 'NH_perk_crossfire')

    def test_metadata_fails_closed_for_missing_changed_and_extra_fields(self):
        base = self.row(ALLIANCE_CONDITIONS[0])
        changes = [('schemaVersion', True), ('schemaVersion', 3.0), ('capturedPolicy', {}),
                   ('capturedPolicy', {'sylvanTacticsPolicy': 1}),
                   ('capturedPolicy', {'sylvanTacticsPolicy': True}),
                   ('capturedPolicy', {'sylvanTacticsPolicy': 2, 'other': 1}),
                   ('id', 'unknown:perk'), ('capturedName', 'hunting pack'),
                   ('icon44', 'NH_perk_elven_precision'), ('historicalIcon44', 'NH_perk_neutral'),
                   ('resources', base['resources'][:-1]),
                   ('resources', base['resources'][:-1]+[base['resources'][0]]),
                   ('resolverSourceSha256', SYLVAN_ADAPTER_SHA256), ('masterSha256', 'bad'),
                   ('native44Sha256', 'bad'), ('selectionEvidenceSha256', 'bad'),
                   ('exportManifestSha256', 'bad'), ('origin', '/private/input'),
                   ('selection', 'unselected'), ('approval', 'Final'), ('alphaMaximum44', 254),
                   ('alphaMaximum32', True)]
        for key, value in changes:
            row = copy.deepcopy(base); row[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_perk_presentation(row, self.header, self.old, self.wrapper)
        for key in base:
            row = copy.deepcopy(base); del row[key]
            with self.subTest(missing=key), self.assertRaises(ValueError):
                validate_perk_presentation(row, self.header, self.old, self.wrapper)
        row = dict(base, prompt='private authoring text')
        with self.assertRaises(ValueError):
            validate_perk_presentation(row, self.header, self.old, self.wrapper)

    def test_source_mutations_invalid_policies_and_wrong_names_are_rejected(self):
        row = self.row(ALLIANCE_CONDITIONS[0])
        for wrapper in (None, self.wrapper+'\n', self.wrapper.replace('usesSylvanTacticsV2', 'usesSylvanTactics'),
                        self.wrapper.replace('Hunting Pack', 'Fleetfoot'),
                        self.wrapper.replace('NH_perk_sylvan_tactics_wild_hunt', 'NH_other'),
                        self.wrapper.replace('return newHorizonsPerkIcon', 'return otherIcon')):
            with self.assertRaises(ValueError):
                validate_perk_presentation(row, self.header, self.old, wrapper)
        for old in (None, self.old+'\n'):
            with self.assertRaises(ValueError):
                validate_perk_presentation(row, self.header, old, self.wrapper)
        for invalid in (True, 0, 3, '2', None):
            with self.assertRaises(ValueError): self.resolve(ALLIANCE_CONDITIONS[0], {'sylvanTacticsPolicy': invalid})
        for name in ('', 'Hunting pack', 'Unknown', 'Translated Hunting Pack'):
            self.assertEqual(self.resolve((row['id'], name), {'sylvanTacticsPolicy': 2}), row['historicalIcon44'])
        changed = self.header.replace('"NH_perk_elven_precision"', '"NH_other"')
        with self.assertRaises(ValueError):
            validate_perk_presentation(row, changed, self.old, self.wrapper)

    def test_family_cannot_be_partial_duplicated_or_mixed_with_old_schema(self):
        rows = [self.row(condition) for condition in ALLIANCE_CONDITIONS]
        validate_alliance_family(rows, self.header, self.old, self.wrapper)
        validate_alliance_family([], self.header, self.old, None)
        for damaged in (rows[:-1], rows+[rows[0]], rows[:-1]+[rows[0]]):
            with self.assertRaises(ValueError): validate_alliance_family(damaged, self.header, self.old, self.wrapper)
        mixed = copy.deepcopy(rows); mixed[0]['schemaVersion'] = 2
        with self.assertRaises(ValueError): validate_alliance_family(mixed, self.header, self.old, self.wrapper)

    def test_schema_three_payload_preserves_native_alpha_recipe_and_clear_perimeter(self):
        row = self.row(ALLIANCE_CONDITIONS[0]); stem = 'SPRITES/'+row['icon44']; data = {}
        def encode(im):
            out = BytesIO(); im.save(out, format='PNG'); return out.getvalue()
        normal = Image.new('RGBA', (44, 44)); normal.paste((180, 120, 60, 255), (4, 4, 40, 40))
        small = Image.new('RGBA', (32, 32)); small.paste((180, 120, 60, 255), (3, 3, 29, 29))
        data[stem+'.png'] = encode(normal); data[stem+'_32.png'] = encode(small)
        states = dict(normal=normal, pressed=ImageEnhance.Brightness(normal).enhance(.82),
                      disabled=ImageEnhance.Brightness(ImageEnhance.Color(normal).enhance(.15)).enhance(.65),
                      highlighted=ImageEnhance.Brightness(normal).enhance(1.13))
        data[stem+'.json'] = json.dumps({'images': [{'group': 0, 'frame': i, 'file': row['icon44']+'_'+s+'.png'}
                                                   for i, s in enumerate(states)]}).encode()
        for key, image in states.items(): data[stem+'_'+key+'.png'] = encode(image)
        for size, name in ((44, stem+'.png'), (32, stem+'_32.png')):
            row['native'+str(size)+'Sha256'] = hashlib.sha256(data[name]).hexdigest()
        validate_perk_payload(row, data.__getitem__)
        for target in (stem+'.json', stem+'_pressed.png', stem+'_32.png'):
            damaged = dict(data)
            if target.endswith('.json'): damaged[target] = b'{"images":[]}'
            else:
                im = Image.open(BytesIO(damaged[target])).convert('RGBA'); im.putpixel((0, 0), (255, 255, 255, 255))
                damaged[target] = encode(im)
            damaged_row = dict(row)
            if target.endswith('_32.png'):
                damaged_row['native32Sha256'] = hashlib.sha256(damaged[target]).hexdigest()
            with self.subTest(target=target), self.assertRaises(ValueError):
                validate_perk_payload(damaged_row, damaged.__getitem__)
        damaged = dict(data); damaged[stem+'_pressed.png'] = data[stem+'_normal.png']
        with self.assertRaises(ValueError): validate_perk_payload(row, damaged.__getitem__)


if __name__ == '__main__':
    unittest.main()
