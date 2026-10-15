"""External additive importer success/fault/missing-input controls."""
import json
import csv
import io
import shutil
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
from PIL import Image
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import import_new_horizons_art_wave as importer
import nhart
from . import test_perk_art_importer as fixture
snapshot = fixture.snapshot


class ArtWaveImporterTest(unittest.TestCase):
    def setUp(self):
        fixture.PerkArtImporterTest.setUp(self)
        inventory = self.repo / importer.INVENTORY
        rows = list(csv.DictReader(io.StringIO(inventory.read_text())))
        rows.append({'Item / ID': 'Arcane Breach (new-horizons:arcaneBreach)', 'Art': 'Not done',
                     'Live binding / source': 'Synthetic status', 'Remaining work / evidence': 'Synthetic'})
        text = io.StringIO(); writer = csv.DictWriter(text, fieldnames=list(rows[0]), lineterminator='\n')
        writer.writeheader(); writer.writerows(rows); inventory.write_text(text.getvalue())
        config_path = self.repo / importer.spells.CONFIG; config_path.parent.mkdir(parents=True)
        config_path.write_text(json.dumps({'arcaneBreach': {'name': 'status', 'active': False,
            'graphics': {role: 'placeholder' for role in importer.spells.ROLES}}, 'core:magicArrow': {'power': 1}}, indent='\t'))
        artist = self.root / 'artist'; artist.mkdir()
        exports = []
        for size in (44, 32):
            source = self.root / f'native{size}.png'; target = artist / f'test-{size}.png'
            target.write_bytes(source.read_bytes()); exports.append({'file': target.name, 'sha256': importer.perks.digest(target)})
        for name in ('master.png', 'test-comparison.png', 'PROMPT-AND-PROVENANCE.md', 'REVIEW.md'):
            target = artist / name; target.write_text('Synthetic fixture, not artwork')
            exports.append({'file': name, 'sha256': importer.perks.digest(target)})
        manifest = artist / 'test-manifest.json'; manifest.write_text(json.dumps({'name': 'test', 'outputs': exports}))
        selected = {'id': 'new-horizons:test.perk', 'name': 'Test', 'selected_review_directory': str(artist),
            'manifest': str(manifest), 'review': str(artist / 'REVIEW.md'), 'comparison': str(artist / 'test-comparison.png'),
            'native44': str(artist / 'test-44.png'), 'native32': str(artist / 'test-32.png')}
        self.handoff = artist / 'HANDOFF.json'; self.handoff.write_text(json.dumps({'count': 1, 'entries': [selected]}))
        status = self.root / 'status'; status.mkdir(); icon = status / importer.STATUS_NAME
        image = Image.new('RGBA', (30, 30), (40, 80, 120, 255)); image.putpixel((0, 0), (0, 0, 0, 0)); image.save(icon)
        hashes = {icon.name: importer.perks.digest(icon)}
        refs = {}
        for role in ('master', 'prompt', 'preview'):
            path = status / (role + '.txt'); path.write_text('Synthetic status fixture'); hashes[path.name] = importer.perks.digest(path); refs[role] = path.name
        self.status = status / 'HANDOFF.json'; self.status.write_text(json.dumps({'effect': 'new-horizons:arcaneBreach',
            'castable': False, 'proposedStatusRoles': {'iconEffect': icon.name, 'iconImmune': icon.name}, 'sha256': hashes, **refs}))
        self.before = snapshot(self.repo); self.artist_before = snapshot(artist); self.status_before = snapshot(status)

    def run_import(self, **baseline):
        if not baseline:
            baseline = {'bootstrap_no_ledger': True}
        return importer.prepare(self.repo, [self.handoff], self.status, self.root / 'wave-work', self.root / 'wave-output',
                                **baseline)

    def initial_proof(self):
        proof = {'entries': [], 'newArchiveSha256': importer.perks.digest(self.repo / importer.spells.ARCHIVE),
                 'entryCount': 1}
        (self.repo / importer.PERK_RECORD).write_text(json.dumps(proof))
        return proof

    def prior_ledger(self):
        ledger = {'format': 1, 'extraTop': {'retained': True}, 'waves': [{
            'newArchiveSha256': importer.perks.digest(self.repo / importer.spells.ARCHIVE), 'entryCount': 1,
            'perks': [], 'statuses': [{'privateShape': 'preserve'}], 'reusedBindings': ['legacy'],
            'unknown': {'ordered': [3, 1, 2]}}]}
        (self.repo / importer.LEDGER).write_text(json.dumps(ledger))
        return ledger

    def test_explicit_mode_is_required_and_modes_are_exclusive(self):
        for arguments in ({}, {'expected_ledger_sha256': 'a' * 64, 'bootstrap_no_ledger': True}):
            with self.subTest(arguments=arguments), self.assertRaisesRegex(ValueError, 'Choose expected'):
                importer.prepare(self.repo, [self.handoff], None, self.root / 'wave-work',
                                 self.root / 'wave-output', **arguments)
        self.assert_inputs_unchanged()
        self.assertFalse((self.root / 'wave-work').exists())

    def test_pinned_ledger_preserves_unknown_history_and_initial_record_bytes(self):
        self.initial_proof()
        previous = self.prior_ledger()
        self.before = snapshot(self.repo)
        self.run_import(expected_ledger_sha256=importer.perks.digest(self.repo / importer.LEDGER))
        ledger = json.loads((self.root / 'wave-output' / importer.LEDGER).read_text())
        self.assertEqual(ledger['waves'][0], previous['waves'][0])
        self.assertEqual(ledger['extraTop'], previous['extraTop'])
        self.assertEqual(len(ledger['waves']), 2)
        self.assertFalse((self.root / 'wave-output' / importer.PERK_RECORD).exists())
        self.assert_inputs_unchanged()

    def test_missing_mismatched_or_malformed_pinned_ledger_refuses_before_work(self):
        for content, expected in ((None, 'a' * 64), (b'{}', 'a' * 64),
                                  (b'null', None), (b'{"format":true,"waves":[]}', None),
                                  (b'{"format":1,"waves":null}', None),
                                  (b'{"format":1,"waves":[]}', None), (b'{broken', None)):
            with self.subTest(content=content):
                path = self.repo / importer.LEDGER
                if content is not None:
                    path.write_bytes(content)
                else:
                    path.unlink(missing_ok=True)
                pin = expected or importer.perks.digest(path)
                with self.assertRaises(ValueError):
                    self.run_import(expected_ledger_sha256=pin)
                self.assertFalse((self.root / 'wave-work').exists())
                self.assertFalse((self.root / 'wave-output').exists())

    def test_pinned_ledger_latest_anchor_must_match_actual_archive_and_count(self):
        for field, value in (('newArchiveSha256', 'a' * 64), ('entryCount', 2), ('entryCount', True)):
            with self.subTest(field=field, value=value):
                ledger = self.prior_ledger()
                ledger['waves'][-1][field] = value
                (self.repo / importer.LEDGER).write_text(json.dumps(ledger))
                with self.assertRaisesRegex(ValueError, 'does not anchor'):
                    self.run_import(expected_ledger_sha256=importer.perks.digest(self.repo / importer.LEDGER))
                self.assertFalse((self.root / 'wave-work').exists())

    def test_invalid_hash_and_symlink_history_are_not_absence(self):
        self.prior_ledger()
        for pin in ('', 'A' * 64, 'a' * 63, 1):
            with self.subTest(pin=pin), self.assertRaisesRegex(ValueError, 'Invalid expected'):
                self.run_import(expected_ledger_sha256=pin)
        path = self.repo / importer.LEDGER
        saved = self.root / 'saved-ledger.json'
        path.rename(saved)
        path.symlink_to(saved)
        with self.assertRaisesRegex(ValueError, 'symlink'):
            self.run_import(expected_ledger_sha256=importer.perks.digest(saved))
        self.assertFalse((self.root / 'wave-work').exists())

    def test_initial_proof_anchored_bootstrap_succeeds_without_replacing_proof(self):
        self.initial_proof()
        self.before = snapshot(self.repo)
        self.run_import()
        self.assert_inputs_unchanged()
        self.assertFalse((self.root / 'wave-output' / importer.PERK_RECORD).exists())

    def test_advanced_baseline_with_removed_ledger_cannot_bootstrap(self):
        self.initial_proof()
        self.run_import()
        output = self.root / 'wave-output'
        ready = json.loads((output / 'READY.json').read_text())
        for name in ready['files']:
            target = self.repo / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(output / name, target)
        (self.repo / importer.LEDGER).unlink()
        before = snapshot(self.repo)
        for arguments in ({'bootstrap_no_ledger': True}, {'expected_ledger_sha256': 'a' * 64}):
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                importer.prepare(self.repo, [self.handoff], None, self.root / 'lost-work',
                                 self.root / 'lost-output', **arguments)
            self.assertFalse((self.root / 'lost-work').exists())
            self.assertFalse((self.root / 'lost-output').exists())
        self.assertEqual(snapshot(self.repo), before)

    def test_bootstrap_rejects_present_ledger_or_unrecorded_selected_header(self):
        self.prior_ledger()
        with self.assertRaisesRegex(ValueError, 'absent additive'):
            self.run_import()
        (self.repo / importer.LEDGER).unlink()
        (self.repo / importer.HEADER).write_text('static const std::map<std::string,std::string> icons = {'
                                                '{"new-horizons:old.perk", "NH_perk_old"}};')
        with self.assertRaisesRegex(ValueError, 'Unrecorded'):
            self.run_import()
        self.assertFalse((self.root / 'wave-work').exists())

    def test_virgin_bootstrap_rejects_compact_and_multiline_non_neutral_bindings(self):
        for body in ('{"new-horizons:old.perk","NH_perk_old"}',
                     '{\n"new-horizons:old.perk"\n,\n"NH_perk_old"\n},'):
            with self.subTest(body=body):
                (self.repo / importer.HEADER).write_text('static const std::map<std::string,std::string> icons={' + body + '};')
                with self.assertRaisesRegex(ValueError, 'Unrecorded'):
                    self.run_import()
                self.assertFalse((self.root / 'wave-work').exists())

    def test_virgin_bootstrap_refuses_unparseable_or_ambiguous_icons_map(self):
        for header in ('', '{"new-horizons:old.perk","NH_perk_old"}',
                       'static const std::map<std::string,std::string> icons={MACRO_ENTRY};',
                       'static const std::map<std::string,std::string> icons={,};',
                       'static const std::map<std::string,std::string> icons={};' * 2):
            with self.subTest(header=header):
                (self.repo / importer.HEADER).write_text(header)
                with self.assertRaisesRegex(ValueError, 'Cannot recognize complete'):
                    self.run_import()
                self.assertFalse((self.root / 'wave-work').exists())

    def test_virgin_header_accepts_explicit_empty_and_fully_neutral_whitespace_variants(self):
        for body in ('', '  \n', '{"new-horizons:test.perk","NH_perk_neutral"}',
                     '{\n"new-horizons:test.perk",\n"NH_perk_neutral"}, // comment\n'):
            with self.subTest(body=body):
                importer.require_virgin_perk_header('static const std::map<std::string,std::string> icons={' + body + '};')

    def test_bootstrap_rejects_malformed_or_nonanchoring_initial_proof(self):
        for field, value in (('newArchiveSha256', 'a' * 64), ('newArchiveSha256', None),
                             ('entryCount', 2), ('entryCount', None), ('entryCount', True),
                             ('entryCount', 1.5), ('entryCount', '1'), ('entries', None)):
            with self.subTest(field=field, value=value):
                proof = self.initial_proof()
                proof[field] = value
                (self.repo / importer.PERK_RECORD).write_text(json.dumps(proof))
                with self.assertRaises(ValueError):
                    self.run_import()
                self.assertFalse((self.root / 'wave-work').exists())

    def test_history_races_before_output_refuse_without_ready(self):
        original = importer.nhart.pack
        for name, operation in ((importer.LEDGER, 'edit'), (importer.LEDGER, 'delete'),
                                (importer.PERK_RECORD, 'edit'), (importer.PERK_RECORD, 'delete')):
            with self.subTest(name=name, operation=operation):
                self.initial_proof()
                self.prior_ledger()
                pin = importer.perks.digest(self.repo / importer.LEDGER)
                def mutate(*args, **kwargs):
                    result = original(*args, **kwargs)
                    if Path(args[2]) == self.root / 'wave-work/candidate.nhart':
                        path = self.repo / name
                        if operation == 'edit':
                            path.write_bytes(path.read_bytes() + b' ')
                        else:
                            path.unlink()
                    return result
                with patch.object(importer.nhart, 'pack', side_effect=mutate), self.assertRaisesRegex(ValueError, 'history changed'):
                    self.run_import(expected_ledger_sha256=pin)
                self.assertFalse((self.root / 'wave-output').exists())
                shutil.rmtree(self.root / 'wave-work')

    def test_history_appearance_and_late_edit_refuse_ready(self):
        original = importer.nhart.verify
        def mutate(archive, *args, **kwargs):
            result = original(archive, *args, **kwargs)
            if Path(archive) == self.root / 'wave-output' / importer.spells.ARCHIVE:
                (self.repo / importer.LEDGER).write_text('{"format":1,"waves":[]}')
            return result
        with patch.object(importer.nhart, 'verify', side_effect=mutate), self.assertRaisesRegex(ValueError, 'history changed'):
            self.run_import()
        self.assertFalse((self.root / 'wave-output/READY.json').exists())

    def test_existing_output_is_never_replaced_on_refusal(self):
        output = self.root / 'wave-output'
        output.mkdir()
        (output / 'prior.bin').write_bytes(b'previous bytes')
        before = snapshot(output)
        with self.assertRaisesRegex(ValueError, 'Fresh nonoverlapping'):
            self.run_import()
        self.assertEqual(snapshot(output), before)
        self.assert_inputs_unchanged()

    def test_partial_ready_write_never_publishes_a_ready_receipt(self):
        original = Path.write_text
        def fail(path, text, *args, **kwargs):
            if path == self.root / 'wave-output/.READY.json.pending':
                original(path, text[:3], *args, **kwargs)
                raise OSError('injected partial ready write')
            return original(path, text, *args, **kwargs)
        with patch.object(Path, 'write_text', fail), self.assertRaises(OSError):
            self.run_import()
        self.assertFalse((self.root / 'wave-output/READY.json').exists())
        self.assert_inputs_unchanged()

    def test_cli_requires_explicit_pin_or_bootstrap_and_forwards_the_choice(self):
        base = ['importer', '--repo', str(self.repo), '--perk-handoff', str(self.handoff),
                '--work-root', str(self.root / 'cli-work'), '--output-root', str(self.root / 'cli-output')]
        for extra in ([], ['--bootstrap-no-ledger', '--expected-baseline-ledger-sha256', 'a' * 64]):
            with patch.object(sys, 'argv', base + extra), patch('sys.stderr', io.StringIO()), self.assertRaises(SystemExit):
                importer.main()
        for extra, expected in ((['--bootstrap-no-ledger'], {'bootstrap_no_ledger': True, 'expected_ledger_sha256': None}),
                                (['--expected-baseline-ledger-sha256', 'a' * 64],
                                 {'bootstrap_no_ledger': False, 'expected_ledger_sha256': 'a' * 64})):
            with patch.object(sys, 'argv', base + extra), patch.object(importer, 'prepare', return_value={}) as prepare, patch('sys.stdout', io.StringIO()):
                importer.main()
            self.assertEqual(prepare.call_args.kwargs, expected)

    def assert_inputs_unchanged(self):
        self.assertEqual(snapshot(self.repo), self.before); self.assertEqual(snapshot(self.handoff.parent), self.artist_before)
        self.assertEqual(snapshot(self.status.parent), self.status_before)

    def test_success_is_external_and_keeps_previous_selection_proofs(self):
        result = self.run_import(); self.assertEqual(result['entryCount'], 9); self.assertEqual(result['addedResources'], 8)
        self.assert_inputs_unchanged(); self.assertTrue((self.root / 'wave-output/READY.json').exists())
        ledger = json.loads((self.root / 'wave-output' / importer.LEDGER).read_text())
        self.assertEqual(len(ledger['waves']), 1); self.assertEqual(ledger['waves'][0]['baselineCount'], 1)
        self.assertFalse((self.root / 'wave-output' / importer.PERK_RECORD).exists())

    def test_final_archive_failure_never_changes_inputs_or_emits_ready(self):
        original = importer.shutil.copyfile
        def fail(source, target):
            if target == self.root / 'wave-output' / importer.spells.ARCHIVE:
                raise OSError('injected final archive failure')
            return original(source, target)
        with patch.object(importer.shutil, 'copyfile', side_effect=fail):
            with self.assertRaises(OSError): self.run_import()
        self.assert_inputs_unchanged(); self.assertFalse((self.root / 'wave-output/READY.json').exists())

    def test_final_ledger_failure_never_changes_inputs_or_emits_ready(self):
        original = Path.write_text
        def fail(path, *args, **kwargs):
            if path == self.root / 'wave-output' / importer.LEDGER:
                raise OSError('injected final ledger failure')
            return original(path, *args, **kwargs)
        with patch.object(Path, 'write_text', fail):
            with self.assertRaises(OSError): self.run_import()
        self.assert_inputs_unchanged(); self.assertFalse((self.root / 'wave-output/READY.json').exists())

    def test_missing_native_input_does_not_count_as_completed(self):
        (self.handoff.parent / 'test-32.png').unlink()
        with self.assertRaises(OSError): self.run_import()
        self.assertEqual(snapshot(self.repo), self.before); self.assertFalse((self.root / 'wave-output').exists())

    def test_duplicate_semantic_selection_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            importer.prepare(self.repo, [self.handoff, self.handoff], self.status, self.root / 'wave-work', self.root / 'wave-output', bootstrap_no_ledger=True)
        self.assert_inputs_unchanged(); self.assertFalse((self.root / 'wave-output').exists())

    def test_preparation_inside_perk_handoff_root_is_rejected(self):
        work = self.handoff.parent / 'fresh-work'
        with self.assertRaisesRegex(ValueError, 'protected artist'):
            importer.prepare(self.repo, [self.handoff], self.status, work, self.root / 'wave-output')
        self.assert_inputs_unchanged(); self.assertFalse(work.exists())

    def test_output_inside_status_handoff_root_is_rejected(self):
        output = self.status.parent / 'fresh-output'
        with self.assertRaisesRegex(ValueError, 'protected artist'):
            importer.prepare(self.repo, [self.handoff], self.status, self.root / 'wave-work', output)
        self.assert_inputs_unchanged(); self.assertFalse(output.exists())

    def test_external_handoff_still_protects_selected_native_directory(self):
        inbox = self.root / 'inbox'; inbox.mkdir()
        handoff = inbox / 'HANDOFF.json'; handoff.write_bytes(self.handoff.read_bytes())
        output = self.handoff.parent / 'fresh-output'
        with self.assertRaisesRegex(ValueError, 'protected artist'):
            importer.prepare(self.repo, [handoff], self.status, self.root / 'wave-work', output, bootstrap_no_ledger=True)
        self.assert_inputs_unchanged(); self.assertFalse(output.exists())

    def test_resolved_artist_symlink_alias_cannot_hide_overlap(self):
        alias = self.root / 'artist-alias'; alias.symlink_to(self.handoff.parent, target_is_directory=True)
        output = alias / 'fresh-output'
        with self.assertRaisesRegex(ValueError, 'protected artist'):
            importer.prepare(self.repo, [self.handoff], self.status, self.root / 'wave-work', output)
        self.assert_inputs_unchanged(); self.assertFalse(output.exists())

    def test_root_containing_artist_inputs_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'protected artist'):
            importer.reject_artist_overlap([self.root.resolve()], [self.handoff.parent])
        self.assert_inputs_unchanged()

    def test_second_wave_appends_ledger_without_rewriting_first_proof(self):
        self.run_import()
        output = self.root / 'wave-output'
        first = json.loads((output / importer.LEDGER).read_text())['waves'][0]
        ready = json.loads((output / 'READY.json').read_text())
        for name in ready['files']:
            target = self.repo / name; target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(output / name, target)
        registry_path = self.repo / 'config/newHorizonsPerks.json'
        registry = json.loads(registry_path.read_text())
        registry['skills']['skill']['perks'].append({'id': 'new-horizons:test.second'})
        registry_path.write_text(json.dumps(registry))
        inventory = self.repo / importer.INVENTORY
        rows = list(csv.DictReader(io.StringIO(inventory.read_text())))
        rows.append({'Item / ID': 'Second (new-horizons:test.second)', 'Art': 'Not done',
                     'Live binding / source': 'Synthetic', 'Remaining work / evidence': 'Synthetic'})
        text = io.StringIO(); writer = csv.DictWriter(text, fieldnames=list(rows[0]), lineterminator='\n')
        writer.writeheader(); writer.writerows(rows); inventory.write_text(text.getvalue())
        selected = json.loads(self.handoff.read_text())
        selected['entries'][0].update({'id': 'new-horizons:test.second', 'name': 'Second'})
        source_manifest = Path(selected['entries'][0]['manifest'])
        manifest = json.loads(source_manifest.read_text()); manifest['name'] = 'second'; source_manifest.write_text(json.dumps(manifest))
        second_handoff = self.handoff.parent / 'SECOND.json'; second_handoff.write_text(json.dumps(selected))
        before = snapshot(self.repo)
        result = importer.prepare(self.repo, [second_handoff], None, self.root / 'second-work', self.root / 'second-output',
                                  expected_ledger_sha256=importer.perks.digest(self.repo / importer.LEDGER))
        ledger = json.loads((self.root / 'second-output' / importer.LEDGER).read_text())
        self.assertEqual(ledger['waves'][0], first); self.assertEqual(len(ledger['waves']), 2)
        self.assertEqual(result['entryCount'], 16)
        self.assertEqual(ledger['waves'][1]['baselineArchiveSha256'], first['newArchiveSha256'])
        self.assertEqual(snapshot(self.repo), before)

    def test_combined_statuses_and_scenario_reuse_select_no_companions(self):
        config_path = self.repo / importer.spells.CONFIG
        config = importer.spells.parse_jsonc(config_path.read_text())
        config['lucidity'] = {'name': 'Lucidity', 'level': 0, 'graphics': {role: 'placeholder' for role in importer.spells.ROLES}}
        config_path.write_text(json.dumps(config, indent='\t'))
        mass = self.repo / importer.MASS_CONFIG
        mass.write_text(json.dumps({'massRegeneration': {'name': 'Mass Regeneration', 'cost': 6,
            'graphics': {'iconScenarioBonus': 'placeholder', 'iconBook': 'unchanged'}}}, indent='\t'))
        inventory = self.repo / importer.INVENTORY
        rows = list(csv.DictReader(io.StringIO(inventory.read_text())))
        for row in rows: row.update({'Area': 'Synthetic', 'Implementation': 'Provisional'})
        text = io.StringIO(); writer = csv.DictWriter(text, fieldnames=list(rows[0]), lineterminator='\n')
        writer.writeheader(); writer.writerows(rows); inventory.write_text(text.getvalue())
        manifest_path = self.repo / importer.spells.MANIFEST
        manifest = json.loads(manifest_path.read_text()); inputs = self.root / 'baseline-input'
        companion = inputs / 'regeneration.png'; Image.new('RGBA', (32, 32), (30, 70, 90, 255)).save(companion)
        manifest['entries'].append({'resource': 'SPRITES/NH_regeneration_32.png', 'source': companion.name,
            'size': companion.stat().st_size, 'sha256': importer.perks.digest(companion),
            'family': 'builtin-skills-perks-orders-and-ui', 'origin': 'synthetic', 'selection': 'test', 'approval': 'test'})
        manifest['requiredFamilyCounts']['builtin-skills-perks-orders-and-ui'] = 2
        manifest_path.write_text(json.dumps(manifest)); nhart.pack(manifest, inputs, self.repo / importer.spells.ARCHIVE)
        folder = self.root / 'lucidity'; folder.mkdir(); art = folder / 'lucidity'; art.mkdir()
        hashes, outputs = {}, []
        for size in (30, 32, 44):
            name = 'NH_status_lucidity-' + str(size) + '.png'
            image = Image.new('RGBA', (size, size), (90, 110, 150, 255)); image.putpixel((0, 0), (0, 0, 0, 0)); image.save(art / name)
            hashes[name] = importer.perks.digest(art / name); outputs.append({'file': name, 'sha256': hashes[name]})
        for name in ('master.png', 'PROMPT.md', 'NH_status_lucidity-comparison.png'):
            (art / name).write_text('Synthetic fixture'); hashes[name] = importer.perks.digest(art / name)
            outputs.append({'file': name, 'sha256': hashes[name]})
        (art / 'NH_status_lucidity-manifest.json').write_text(json.dumps({'outputs': outputs}))
        handoff = folder / 'HANDOFF.json'; handoff.write_text(json.dumps({'assets': [{
            'id': 'new-horizons:lucidity', 'directory': 'lucidity', 'primaryRole': 'effect-and-immune-30px', 'sha256': hashes}]}))
        before = snapshot(self.repo)
        result = importer.prepare(self.repo, [self.handoff], [self.status, handoff], self.root / 'combined-work',
                                  self.root / 'combined-output', mass_regeneration_scenario=True, bootstrap_no_ledger=True)
        self.assertEqual(result['statusCount'], 2); self.assertEqual(result['addedResources'], 9)
        ledger = json.loads((self.root / 'combined-output' / importer.LEDGER).read_text())
        self.assertEqual(len(ledger['waves'][0]['statuses']), 2)
        self.assertEqual(len(ledger['waves'][0]['reusedBindings']), 1)
        selected = set(ledger['waves'][0]['resources'])
        self.assertIn('SPRITES/NH_status_lucidity-30.png', selected)
        self.assertNotIn('SPRITES/NH_status_lucidity-32.png', selected)
        self.assertNotIn('SPRITES/NH_status_lucidity-44.png', selected)
        self.assertEqual(snapshot(self.repo), before)


if __name__ == '__main__':
    unittest.main()
