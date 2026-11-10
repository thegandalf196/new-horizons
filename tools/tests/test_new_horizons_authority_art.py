"""Owner-supplied Authority exports, alpha, native ranks and baseline lineage."""
import hashlib
import json
from pathlib import Path
import re
import sys
import unittest

from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import prepare_new_horizons_authority_art as authority
from .nhart_test_resources import read_resource, open_image, later_art_waves
from .test_new_horizons_art_additions import perk_header_history_forms


class AuthorityExportAlgorithmTest(unittest.TestCase):
    def test_nonrectangular_partition_preserves_alpha_and_records_distant_specks(self):
        source = Image.new('RGBA', (1448, 1086))
        draw = ImageDraw.Draw(source)
        for index in range(11):
            x, y = (index % 4) * 362 + 181, (index // 4) * 362 + 181
            draw.ellipse((x - 120, y - 120, x + 120, y + 120), fill=(50 + index * 10, 90, 120, 197))
        source.putpixel((1300, 940), (200, 150, 10, 7))
        original = source.tobytes()
        foregrounds, records, components, partitions = authority.split_sheet(source)
        reconstructed = Image.new('RGBA', source.size)
        for row, partition in zip(records, partitions):
            reconstructed.alpha_composite(partition, tuple(row['fullPartitionAlphaBounds'][:2]))
        self.assertEqual(reconstructed.getchannel('A').tobytes(), source.getchannel('A').tobytes())
        self.assertEqual(sum(row['alphaMass'] for row in records), sum(source.getchannel('A').tobytes()))
        self.assertEqual(sum(row['excludedAlphaMass'] for row in records), 7)
        self.assertEqual(len(foregrounds), 11)
        self.assertEqual({row['owner'] for row in components}, set(range(1, 12)))
        self.assertEqual(source.tobytes(), original)

    def test_reduction_does_not_bleed_hidden_rgb_or_square_alpha(self):
        source = Image.new('RGBA', (100, 80), (255, 0, 0, 0))
        source.paste((0, 200, 0, 128), (10, 10, 90, 70))
        result = authority.fit_foreground(source, (32, 32), (2, 2, 30, 30))
        pixels = [pixel for pixel in result.get_flattened_data() if pixel[3]]
        self.assertTrue(pixels)
        self.assertTrue(all(red == 0 and blue == 0 for red, green, blue, alpha in pixels))
        self.assertTrue(120 <= max(pixel[3] for pixel in pixels) <= 150)
        self.assertEqual(result.getchannel('A').crop((0, 0, 32, 2)).getextrema(), (0, 0))
        self.assertEqual(source.getpixel((0, 0)), (255, 0, 0, 0))

    def test_states_change_visibility_without_changing_the_silhouette(self):
        source = Image.new('RGBA', (44, 44))
        source.paste((190, 80, 25, 137), (5, 5, 39, 39))
        states = authority.button_states(source)
        self.assertEqual(len({image.tobytes() for image in states}), 4)
        for image in states:
            self.assertEqual(image.getchannel('A').tobytes(), source.getchannel('A').tobytes())

    def test_historical_header_reversal_is_exact_and_rejects_unknown_changes(self):
        block = authority.binding_block()
        historical = '#pragma once\n// synthetic baseline\n'
        self.assertIn(historical, perk_header_history_forms(block + historical))
        for changed in (block.replace('offensive_drills', 'unknown'), block + block):
            with self.assertRaisesRegex(ValueError, 'Authority binding block'):
                perk_header_history_forms(changed + historical)
        self.assertNotIn(historical, perk_header_history_forms(block + historical + '// unrelated\n'))


class AuthoritySelectedPayloadTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.record_path = ROOT / authority.RECORD
        cls.record = json.loads(cls.record_path.read_text())
        cls.manifest = json.loads((ROOT / authority.MANIFEST).read_text())
        cls.entries = {row['resource']: row for row in cls.manifest['entries']}

    def test_exact_owner_source_and_separate_selection_approval_and_rights(self):
        record = self.record
        self.assertEqual(record['sourceSheetSha256'], authority.SHEET_SHA256)
        self.assertEqual((record['sourceSize'], record['sourceMode'], record['sourceMetadataKeys']),
                         ([1448, 1086], 'RGBA', []))
        self.assertEqual(record['origin'], authority.ORIGIN)
        self.assertEqual(record['selection'], authority.SELECTION)
        self.assertIn('no Final approval', record['approval'])
        self.assertEqual(record['rights'], authority.RIGHTS)
        self.assertIn('owner-confirmed ChatGPT-generated', record['origin'])
        self.assertIn('not generation by this agent', record['origin'])
        self.assertIn('for New Horizons distribution', record['selection'])
        self.assertIn('Various / CC BY-SA 4.0 declaration and mixed Heroes III provenance', record['rights'])
        self.assertIn('Third-party rights and license obligations are not waived.', record['rights'])
        self.assertNotIn('original authorship and license unspecified', json.dumps(record))
        self.assertNotRegex(json.dumps(record), r'(?i)/home/|/users/|[a-z]:[\\/]|\\\\')
        self.assertEqual([(row['id'], row['name']) for row in record['subjects']], list(authority.IDENTITIES))
        for row in record['subjects']:
            self.assertLess(row['excludedAlphaMaximum'], 32)
            self.assertGreater(row['partialAlphaPixels'], 100)
            self.assertLess(row['excludedAlphaMass'], row['alphaMass'] / 1000)

    def test_exact_additive_selection_and_untouched_previous_wave_objects(self):
        record = self.record
        self.assertEqual(len(record['sourceEntries']), 82)
        self.assertEqual(len(set(record['resources'])), 82)
        for row in record['sourceEntries']:
            self.assertEqual(self.entries[row['resource']], row)
            self.assertEqual(hashlib.sha256(read_resource(row['resource'])).hexdigest(), row['sha256'])
            self.assertFalse((ROOT / row['source']).exists())
        wave = later_art_waves()[-1]
        self.assertEqual((wave['baselineCount'], wave['entryCount']), (5728, 5810))
        self.assertEqual(wave['resources'], record['resources'])
        self.assertEqual(wave['suppliedAuthority'][0]['selectionRecordSha256'],
                         hashlib.sha256(self.record_path.read_bytes()).hexdigest())
        for key in ('origin', 'selection', 'approval'):
            self.assertEqual(wave['suppliedAuthority'][0][key], record[key])

    def test_confirmed_origin_preserves_exact_selected_payloads_and_template_credits(self):
        entries = sorted(({key: row[key] for key in ('resource', 'size', 'sha256')}
                          for row in self.record['sourceEntries']), key=lambda row: row['resource'])
        fingerprint = authority.canonical_digest(entries)
        self.assertEqual(fingerprint, '85e6162e18b13131ed788bdb725a34d3b941ad7753b5718f7c69c278f0fd332f')
        composite_suffix = ('; native modder-tools-pack mixed-origin leather and selected '
                            'Battlecraft rank-marker composites; inherited terms retained')
        for row in self.record['sourceEntries']:
            expected = authority.ORIGIN
            if row['resource'].startswith('SPRITES/NH_authority_'):
                expected += composite_suffix
            self.assertEqual(row['origin'], expected)
            self.assertEqual(row['selection'], authority.SELECTION)
            self.assertEqual(row['approval'], authority.APPROVAL)
        self.assertEqual(self.record['templates'], authority.TEMPLATE_HASHES)
        for name, expected in self.record['rankMarkerSources'].items():
            self.assertEqual(hashlib.sha256(read_resource(name)).hexdigest(), expected)

    def test_twelve_skill_roles_native_geometry_protected_border_and_rank_markers(self):
        self.assertEqual([row['rank'] for row in self.record['ranks']], list(authority.RANKS))
        for role, (_, size, rectangle, marker_box) in authority.SLOTS.items():
            variants = []
            for row in self.record['ranks']:
                name = row['images'][role]
                self.assertEqual(name, f'NH_authority_{row["rank"]}_{role}.png')
                image = open_image('SPRITES/' + name)
                self.assertEqual((image.size, image.mode, image.info), (size, 'RGBA', {}))
                marker_role = 'medium' if role == 'scenarioBonus' else role
                marker_source = open_image(f'SPRITES/NH_battlecraft_{row["rank"]}_{marker_role}.png').convert('RGBA')
                box = marker_box or authority.SLOTS['medium'][3]
                target = box if marker_box else (19, 56, 39, 62)
                self.assertEqual(image.crop(target).tobytes(), marker_source.crop(box).tobytes())
                variants.append(image)
            for other in variants[1:]:
                difference = ImageChops.difference(variants[0], other)
                difference.putalpha(255)
                # Every rank-dependent difference stays within the native marker.
                marker = self.record['slots'][role]
                x, y = marker['rankMarkerPosition']
                bounds = marker['rankMarkerSourceRectangle']
                difference.paste((0, 0, 0, 255), (x, y, x + bounds[2] - bounds[0], y + bounds[3] - bounds[1]))
                self.assertIsNone(difference.convert('RGB').getbbox())
            self.assertEqual(len({image.tobytes() for image in variants}), 3)

    def test_ten_perk_bindings_aliases_native_insets_and_alpha_preserving_states(self):
        header = (ROOT / authority.HEADER).read_text()
        bindings = dict(re.findall(r'\{"(new-horizons:[^"]+)", "([^"]+)"\}', header))
        self.assertEqual(header.count(authority.binding_block()), 1)
        self.assertEqual(len(self.record['perks']), 10)
        for row in self.record['perks']:
            stem = row['icon44']
            self.assertEqual(bindings[row['id']], stem)
            descriptor = json.loads(read_resource('SPRITES/' + stem + '.json'))
            self.assertEqual(descriptor, {'images': [
                {'group': 0, 'frame': index, 'file': stem + '_' + state + '.png'}
                for index, state in enumerate(authority.STATES)]})
            for size, suffix in ((44, '.png'), (32, '_32.png')):
                image = open_image('SPRITES/' + stem + suffix)
                self.assertEqual((image.mode, image.size, image.info), ('RGBA', (size, size), {}))
                left, top, right, bottom = image.getchannel('A').getbbox()
                self.assertTrue(2 <= left < right <= size - 2 and 2 <= top < bottom <= size - 2)
            normal = open_image('SPRITES/' + stem + '.png')
            for state, expected in zip(authority.STATES, authority.button_states(normal)):
                actual = open_image('SPRITES/' + stem + '_' + state + '.png')
                self.assertEqual(actual.tobytes(), expected.tobytes())


if __name__ == '__main__':
    unittest.main()
