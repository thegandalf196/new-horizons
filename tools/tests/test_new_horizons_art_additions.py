"""Explicit additive-wave proof; no global current-package/perk-count freeze."""
import csv
import hashlib
import io
import json
from pathlib import Path
import re
import sys
import unittest
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import import_new_horizons_art_wave as importer
from .nhart_perk_presentations import (header_before_perk_presentation, validate_perk_presentation,
    validate_perk_payload, REWORKED_PERK_REBINDINGS, header_before_reworked_perks)
from .nhart_sylvan_perk_presentations import validate_sylvan_family
from .nhart_config_revisions import config_at_revision, wave_config_revision
from .nhart_legacy_art_lineage import validate_art_roles
from .nhart_hero_interface_buttons import validate_hero_interface_buttons
from .nhart_specialty_lineage import specialty_family
from .nhart_test_resources import later_art_waves, later_art_resources, read_resource, open_image, verified_index, entries_before_wave, resource_before_wave


# Accepted faction rebindings change presentation identities, not selected bytes.
# Keep the original wave ledger/hashes intact and reverse only these exact edits
# when checking its historical header. This is test-only lineage, not a loader.
FACTION_BINDINGS = '''		{"new-horizons:metamagic.arcaneCatalyst", "NH_perk_arcane_acquisition"},
		{"new-horizons:metamagic.arcaneShield", "NH_perk_echoed_duration"},
		{"new-horizons:metamagic.manaConduit", "NH_perk_arcane_economy"},
		{"new-horizons:metamagic.lingeringMagic", "NH_perk_spell_sequencing"},
		{"new-horizons:metamagic.arcaneFeedback", "NH_perk_focused_pairing"},
		{"new-horizons:metamagic.spellFracture", "NH_perk_perfect_sequence"},
		{"new-horizons:metamagic.arcaneWellspring", "NH_perk_countersequence"},
		{"new-horizons:metamagic.arcaneAttunement", "NH_perk_spell_buffer_v2"},
		{"new-horizons:metamagic.arcaneEcho", "NH_perk_formula_reserve"},
		{"new-horizons:metamagic.arcaneSingularity", "NH_perk_grand_metamagic"},
		{"new-horizons:divineMandate.knightlyValor", "NH_perk_knightly_sequence"},
		{"new-horizons:divineMandate.shieldOfFaith", "NH_perk_shared_purpose"},
		{"new-horizons:divineMandate.righteousDiscipline", "NH_perk_crown_and_altar"},
		{"new-horizons:divineMandate.unwaveringStandard", "NH_perk_royal_standard"},
		{"new-horizons:divineMandate.inspiringLeadership", "NH_perk_divine_discipline"},
		{"new-horizons:divineMandate.consecratedArms", "NH_perk_purifying_mandate"},
		{"new-horizons:divineMandate.divineFury", "NH_perk_consecrated_casting"},
		{"new-horizons:demonicGating.infernalDiscipline", "NH_perk_reserve_discipline"},
		{"new-horizons:demonicGating.infernalOnslaught", "NH_perk_mobile_gate"},
		{"new-horizons:demonicGating.hellstride", "NH_perk_chain_gate"},
'''
FACTION_RENAMES = {
    'new-horizons:shroudOfMalassa.ambusher': ('new-horizons:shroudOfMalassa.shadowHex', 'NH_perk_ambusher'),
    'new-horizons:shroudOfMalassa.nightProwler': ('new-horizons:shroudOfMalassa.exposedWeakness', 'NH_perk_night_prowler'),
    'new-horizons:elementalRebirth.elementalMemory': ('new-horizons:elementalRebirth.elementalConduit', 'NH_perk_elemental_memory'),
    'new-horizons:elementalRebirth.elementalWard': ('new-horizons:elementalRebirth.elementalCataclysm', 'NH_perk_elemental_ward'),
}
WAVE_CURRENT_IDS = {
    **{old: current for old, (current, _) in REWORKED_PERK_REBINDINGS.items()},
    **{old: current for old, (current, _) in FACTION_RENAMES.items()},
    'new-horizons:divineMandate.sharedPurpose': 'new-horizons:divineMandate.shieldOfFaith',
    'new-horizons:divineMandate.consecratedCasting': 'new-horizons:divineMandate.divineFury',
    'new-horizons:divineMandate.royalStandard': 'new-horizons:divineMandate.unwaveringStandard',
    'new-horizons:divineMandate.divineDiscipline': 'new-horizons:divineMandate.inspiringLeadership',
    'new-horizons:divineMandate.knightlySequence': 'new-horizons:divineMandate.knightlyValor',
}


def header_before_faction_rebindings(header):
    if header.count(FACTION_BINDINGS) != 1:
        raise ValueError('Current faction binding block changed')
    historical = header.replace(FACTION_BINDINGS, '', 1)
    for old, (current, icon) in FACTION_RENAMES.items():
        before = '\t\t{"' + old + '", "' + icon + '"},\n'
        after = '\t\t{"' + current + '", "' + icon + '"},\n'
        if historical.count(after) != 1 or before in historical:
            raise ValueError('Current faction renamed binding changed')
        historical = historical.replace(after, before, 1)
    return historical


def config_at_wave(config, waves, index, config_path):
    """Undo later graphics edits only; historical nongraphics bytes stay checked."""
    historical = json.loads(json.dumps(config))
    for later in reversed(waves[index + 1:]):
        # Producer applies reused roles before new spell roles within a wave.
        edits = later.get('reusedBindings', []) + later.get('spells', [])
        for row in reversed(edits):
            if row['config'] == config_path:
                historical[row['id'].split(':')[1]]['graphics'].update(row['previousBindings'])
    return historical


def perk_header_history_forms(header):
    """Retain exact hashes across the approved inline-map/table extraction."""
    previous = header_before_perk_presentation(header)
    if previous != header:
        return (header,) + perk_header_history_forms(previous)
    if 'newHorizonsPerkIconBindings' not in header:
        return (header,)
    # Undo only the known syntactic extraction. Binding bytes/order and the
    # fallback/lookup body remain untouched, so their edits still break history.
    function = 'inline const std::string & newHorizonsPerkIcon(const std::string & perkId)\n{\n'
    replacements = (
        ('#include <iterator>\n', ''),
        ('#include <utility>\n', ''),
        ('// Constant binding data; the runtime map remains lazy below.\n'
         'inline constexpr std::pair<const char *, const char *> newHorizonsPerkIconBindings[] = {\n',
         function + '\tstatic const std::map<std::string, std::string> icons = {\n'),
        ('};\n\n' + function + '\tstatic const std::map<std::string, std::string> icons(\n'
         '\t\tstd::begin(newHorizonsPerkIconBindings), std::end(newHorizonsPerkIconBindings));\n',
         '\t};\n'),
    )
    historical = header
    for current, previous in replacements:
        if historical.count(current) != 1:
            raise ValueError('Unrecognized perk icon table extraction')
        historical = historical.replace(current, previous, 1)
    return header, historical


class PerkHeaderHistoryTest(unittest.TestCase):
    inline = '''#pragma once

#include <map>
#include <string>

inline const std::string & newHorizonsPerkIcon(const std::string & perkId)
{
	static const std::map<std::string, std::string> icons = {
		{"new-horizons:fixture.first", "NH_first"},
		{"new-horizons:fixture.second", "NH_second"},
	};
	static const std::string fallback = "NH_perk_neutral";
	const auto found = icons.find(perkId);
	return found == icons.end() ? fallback : found->second;
}
'''
    table = '''#pragma once

#include <iterator>
#include <map>
#include <string>
#include <utility>

// Constant binding data; the runtime map remains lazy below.
inline constexpr std::pair<const char *, const char *> newHorizonsPerkIconBindings[] = {
		{"new-horizons:fixture.first", "NH_first"},
		{"new-horizons:fixture.second", "NH_second"},
};

inline const std::string & newHorizonsPerkIcon(const std::string & perkId)
{
	static const std::map<std::string, std::string> icons(
		std::begin(newHorizonsPerkIconBindings), std::end(newHorizonsPerkIconBindings));
	static const std::string fallback = "NH_perk_neutral";
	const auto found = icons.find(perkId);
	return found == icons.end() ? fallback : found->second;
}
'''

    def test_both_recorded_layouts_retain_their_exact_bytes(self):
        self.assertEqual(perk_header_history_forms(self.inline), (self.inline,))
        self.assertEqual(perk_header_history_forms(self.table), (self.table, self.inline))

    def test_bindings_order_fallback_and_lookup_changes_still_break_history(self):
        first = '\t\t{"new-horizons:fixture.first", "NH_first"},\n'
        second = '\t\t{"new-horizons:fixture.second", "NH_second"},\n'
        recorded = {hashlib.sha256(text.encode()).hexdigest() for text in (self.inline, self.table)}
        mutations = (self.table.replace('NH_first', 'NH_changed'),
                     self.table.replace(first + second, second + first),
                     self.table.replace('NH_perk_neutral', 'NH_changed_fallback'),
                     self.table.replace('found == icons.end()', 'found != icons.end()'),
                     '// unrelated change\n' + self.table)
        for changed in mutations:
            with self.subTest(header=changed):
                actual = {hashlib.sha256(text.encode()).hexdigest()
                          for text in perk_header_history_forms(changed)}
                self.assertTrue(recorded.isdisjoint(actual))

    def test_unknown_or_duplicate_extraction_syntax_is_rejected(self):
        for changed in (self.table.replace('std::begin(', 'std::cbegin('),
                        '#include <iterator>\n' + self.table):
            with self.subTest(header=changed):
                with self.assertRaisesRegex(ValueError, 'Unrecognized perk icon table extraction'):
                    perk_header_history_forms(changed)


class ArtWaveHistoryTest(unittest.TestCase):
    def test_mass_regeneration_proof_survives_later_mass_wave_without_loosening_digest(self):
        original = {key: {'power': 7, 'graphics': {role: key + '-' + role
                    for role in importer.spells.ROLES}}
                    for key in (*importer.MASS_SPELLS, 'massRegeneration')}
        reused = {'config': importer.MASS_CONFIG, 'id': 'new-horizons:massRegeneration',
                  'bindings': {'iconScenarioBonus': 'NH_regeneration_32.png'},
                  'previousBindings': {'iconScenarioBonus': original['massRegeneration']['graphics']['iconScenarioBonus']}}
        current = json.loads(json.dumps(original))
        current['massRegeneration']['graphics'].update(reused['bindings'])
        old_digest = importer.non_status_config_digest(current, [reused])
        records = []
        for key in importer.MASS_SPELLS:
            row = {'config': importer.MASS_CONFIG, 'id': 'new-horizons:' + key,
                   'previousBindings': dict(current[key]['graphics']),
                   'bindings': {role: 'selected-' + key + '-' + role for role in importer.spells.ROLES}}
            current[key]['graphics'].update(row['bindings']); records.append(row)
        waves = [{'reusedBindings': [reused]}, {'spells': records}]
        self.assertNotEqual(importer.non_status_config_digest(current, [reused]), old_digest)
        historical = config_at_wave(current, waves, 0, importer.MASS_CONFIG)
        self.assertEqual(importer.non_status_config_digest(historical, [reused]), old_digest)
        self.assertEqual(current['massBless']['graphics'], records[0]['bindings'])
        current['massBless']['power'] += 1
        mutated = config_at_wave(current, waves, 0, importer.MASS_CONFIG)
        self.assertNotEqual(importer.non_status_config_digest(mutated, [reused]), old_digest)

    def test_rollback_includes_later_same_config_reused_roles_in_reverse_order(self):
        base = {'massBless': {'power': 7, 'graphics': {'iconBook': 'original'}}}
        spell = {'config': importer.MASS_CONFIG, 'id': 'new-horizons:massBless',
                 'previousBindings': {'iconBook': 'original'}}
        reused = {'config': importer.MASS_CONFIG, 'id': 'new-horizons:massBless',
                  'previousBindings': {'iconBook': 'first-spell'}}
        unrelated = {'config': 'other.json', 'id': 'new-horizons:absent',
                     'previousBindings': {'iconBook': 'must-not-apply'}}
        current = {'massBless': {'power': 7, 'graphics': {'iconBook': 'latest'}}}
        waves = [{}, {'spells': [spell]}, {'reusedBindings': [reused, unrelated]}]
        self.assertEqual(config_at_wave(current, waves, 0, importer.MASS_CONFIG), base)
        self.assertEqual(config_at_wave(current, waves, 1, importer.MASS_CONFIG),
                         {'massBless': {'power': 7, 'graphics': {'iconBook': 'first-spell'}}})


class ArtAdditionsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.waves = later_art_waves()
        cls.configProofs = json.loads((ROOT / 'assets/new-horizons/runtime-art-config-revisions.json').read_text())
        cls.manifest = json.loads((ROOT / importer.spells.MANIFEST).read_text())
        cls.header = (ROOT / importer.HEADER).read_text()
        adapter = ROOT / 'client/windows/NewHorizonsCapturedPerkIcons.h'
        cls.capturedHeader = adapter.read_text() if adapter.exists() else None
        cls.config = importer.spells.parse_jsonc((ROOT / importer.spells.CONFIG).read_text())

    def test_public_atlas_recipes_keep_bounded_geometry_without_authoring_prompts(self):
        families = [('active-perks-v1', [6, 6, 6, 6], [1536, 1024]),
                    ('sylvan-luck-perks-v1', [3], [2172, 724]),
                    ('sylvan-luck-perks-v2', [4], [2172, 724])]
        for family, counts, dimensions in families:
            with self.subTest(family=family):
                recipe = json.loads((ROOT / 'assets/new-horizons/art-source' / family / 'generation.json').read_text())
                self.assertFalse('promptSummary' in recipe, 'Public recipe must omit authoring summaries')
                self.assertEqual(recipe['license'], 'CC0-1.0')
                atlases = recipe.get('atlases', [recipe])
                self.assertEqual([len(atlas['cells']) for atlas in atlases], counts)
                keys = []
                for atlas in atlases:
                    self.assertFalse('prompt' in atlas, 'Public recipe must omit raw authoring prompts')
                    self.assertEqual(atlas['dimensions'], dimensions)
                    self.assertTrue(atlas.get('file', recipe.get('atlas', '')).endswith('.png'))
                    for cell in atlas['cells']:
                        self.assertTrue(cell['key'].startswith('NH_'))
                        keys.append(cell['key'])
                        if 'atlases' in recipe:
                            self.assertTrue(cell['id'])
                        self.assertEqual(len(cell['box']), 4)
                        self.assertTrue(all(type(value) is int for value in cell['box']))
                        left, top, right, bottom = cell['box']
                        self.assertTrue(0 <= left < right <= dimensions[0])
                        self.assertTrue(0 <= top < bottom <= dimensions[1])
                self.assertEqual(len(keys), len(set(keys)))

    def test_each_wave_has_exact_baseline_payload_and_provenance_proof(self):
        self.assertTrue(self.waves)
        self.assertEqual(len(later_art_resources()), sum(len(wave['resources']) for wave in self.waves))
        for index, wave in enumerate(self.waves):
            baseline = entries_before_wave(self.manifest['entries'], self.waves, index)
            self.assertEqual(len(baseline), wave['baselineCount'])
            self.assertEqual(importer.entries_digest(baseline), wave['baselineEntriesSha256'])
            self.assertEqual(wave['entryCount'], wave['baselineCount'] + len(wave['resources']))
            for row in baseline:
                self.assertEqual(hashlib.sha256(resource_before_wave(row['resource'], self.waves, index)).hexdigest(), row['sha256'])
            if index:
                self.assertEqual(wave['baselineArchiveSha256'], self.waves[index - 1]['newArchiveSha256'])
        self.assertEqual(len(verified_index()), self.waves[-1]['entryCount'])
        self.assertEqual(importer.perks.digest(ROOT / importer.spells.ARCHIVE), self.waves[-1]['newArchiveSha256'])

    def test_exact_perk_bindings_and_native_families(self):
        lookup = dict(re.findall(r'\{"(new-horizons:[^"]+)", "([^"]+)"\}', self.header))
        roster = json.loads((ROOT / 'config/newHorizonsPerks.json').read_text())
        current_ids = {row['id'] for skill in roster['skills'].values() for row in skill['perks']}
        historical = dict(re.findall(r'\{"(new-horizons:[^"]+)", "([^"]+)"\}',
                                     header_before_faction_rebindings(header_before_reworked_perks(self.header))))
        for wave in self.waves:
            declared = {name for row in wave['perks'] + wave['statuses'] + wave.get('spells', []) + wave.get('specialties', []) + wave.get('creatures', []) + wave.get('perkPresentations', []) + wave.get('artRoles', []) + wave.get('heroInterfaceButtons', []) + wave.get('orderEntryButtons', []) + wave.get('nativeEffectAliases', []) for name in row['resources']}
            self.assertEqual(declared, set(wave['resources']))
            for row in wave['perks']:
                current_id = WAVE_CURRENT_IDS.get(row['id'], row['id'])
                self.assertIn(current_id, current_ids)
                self.assertEqual(lookup[current_id], row['icon44'])
                self.assertEqual(historical[row['id']], row['icon44'])
                self.assertEqual(len(row['resources']), 7)
                stem = row['icon44']
                for size in (44, 32):
                    filename = stem + ('' if size == 44 else '_32') + '.png'
                    self.assertEqual(open_image('SPRITES/' + filename).size, (size, size))
                    self.assertEqual(hashlib.sha256(read_resource('SPRITES/' + filename)).hexdigest(), row['native' + str(size) + 'Sha256'])
                frames = json.loads(read_resource('SPRITES/' + stem + '.json'))['images']
                self.assertEqual(frames, [{'group': 0, 'frame': index, 'file': stem + '_' + state + '.png'}
                    for index, state in enumerate(('normal', 'pressed', 'disabled', 'highlighted'))])
                for frame in frames:
                    self.assertEqual(open_image('SPRITES/' + frame['file']).size, (44, 44))

    def test_faction_rebinding_history_is_exact_and_preserves_unrelated_edits(self):
        historical = header_before_faction_rebindings(self.header)
        bindings = dict(re.findall(r'\{"(new-horizons:[^"]+)", "([^"]+)"\}', FACTION_BINDINGS))
        current = json.loads((ROOT / 'config/newHorizonsPerks.json').read_text())
        ids = {perk['id'] for skill in current['skills'].values() for perk in skill['perks']}
        self.assertEqual(len(bindings), 20)
        self.assertTrue(bindings.keys() <= ids)
        for changed in (self.header.replace('NH_perk_arcane_acquisition', 'NH_changed', 1),
                        self.header.replace('NH_perk_ambusher', 'NH_changed', 1),
                        self.header.replace(FACTION_BINDINGS, FACTION_BINDINGS * 2)):
            with self.assertRaises(ValueError):
                header_before_faction_rebindings(changed)
        changed = self.header.replace('NH_perk_neutral', 'NH_changed_neutral')
        self.assertNotEqual(header_before_faction_rebindings(changed), historical)

    def test_optional_perk_presentations_preserve_named_condition_and_historical_shield(self):
        for wave in self.waves:
            validate_sylvan_family(wave.get('perkPresentations', []), self.header, self.capturedHeader)
            for row in wave.get('perkPresentations', []):
                validate_perk_presentation(row, self.header, self.capturedHeader)
                validate_perk_payload(row, read_resource)
                for suffix in ('.json', '_normal.png', '_pressed.png', '_disabled.png', '_highlighted.png'):
                    self.assertIn('SPRITES/' + row['historicalIcon44'] + suffix, verified_index())

    def test_recorded_historical_art_roles_preserve_original_bytes_and_provenance(self):
        manifest = {row['resource']: row for row in self.manifest['entries']}
        for wave in self.waves:
            validate_art_roles(wave, manifest, read_resource)

    def test_selected_hero_interface_buttons_preserve_native_states_and_provenance(self):
        manifest = {row['resource']: row for row in self.manifest['entries']}
        for wave in self.waves:
            validate_hero_interface_buttons(wave.get('heroInterfaceButtons', []), manifest, read_resource)

    def test_selected_orders_entry_preserves_states_safe_inset_and_source_identity(self):
        rows = [row for wave in self.waves for row in wave.get('orderEntryButtons', [])]
        self.assertEqual(len(rows), 1)
        row = rows[0]
        self.assertEqual(row['id'], 'new-horizons:ordersCommandEntry')
        self.assertEqual(row['family'], 'orders-command-entry-button')
        self.assertEqual(row['sourceMasterSha256'],
                         '459aa68a47294fa1a185547a6e8353eb9988ab76bc18db980d01fcf58c920690')
        self.assertEqual(row['nativeCanvas'], [48, 36])
        states = ['normal', 'pressed', 'disabled', 'highlighted']
        self.assertEqual(row['states'], states)
        names = {'SPRITES/NH_orders_command.json'} | {
            'SPRITES/NH_orders_command_' + state + '.png' for state in states}
        self.assertEqual(set(row['resources']), names)
        manifest = {entry['resource']: entry for entry in self.manifest['entries']}
        self.assertEqual({entry['resource'] for entry in row['sourceEntries']}, names)
        for entry in row['sourceEntries']:
            self.assertEqual(manifest[entry['resource']], entry)
            self.assertEqual(hashlib.sha256(read_resource(entry['resource'])).hexdigest(), entry['sha256'])
        descriptor = json.loads(read_resource('SPRITES/NH_orders_command.json'))
        self.assertEqual(descriptor, {'images': [
            {'group': 0, 'frame': index, 'file': 'NH_orders_command_' + state + '.png'}
            for index, state in enumerate(states)]})
        hashes = set()
        for state in states:
            name = 'SPRITES/NH_orders_command_' + state + '.png'
            image = open_image(name)
            self.assertEqual((image.size, image.mode), ((48, 36), 'RGBA'))
            self.assertEqual(image.getchannel('A').getextrema(), (0, 255))
            left, top, right, bottom = image.getchannel('A').getbbox()
            self.assertTrue(3 <= left < right <= 45 and 3 <= top < bottom <= 33)
            hashes.add(hashlib.sha256(read_resource(name)).hexdigest())
        self.assertEqual(len(hashes), 4)

    def test_stronger_poison_alias_preserves_external_frames_and_reserved_palette(self):
        rows = [row for wave in self.waves for row in wave.get('nativeEffectAliases', [])]
        self.assertEqual(len(rows), 1)
        row = rows[0]
        self.assertEqual(row['id'], 'new-horizons:broghildStrongPoison')
        self.assertEqual(row['family'], 'native-specialty-effect-aliases')
        name = 'SPRITES/NH_broghild_poison_red.json'
        self.assertEqual(row['resources'], [name])
        manifest = {entry['resource']: entry for entry in self.manifest['entries']}
        self.assertEqual(row['sourceEntries'], [manifest[name]])
        self.assertNotIn('SPRITES/SP11_.DEF', {key.upper() for key in manifest})
        payload = read_resource(name)
        self.assertEqual(hashlib.sha256(payload).hexdigest(),
                         'ac6b4c5e82f1f1f62c60dbd82caa2bc32f1b077367498f6842b7ef1e516bfac3')
        descriptor = json.loads(payload)
        self.assertEqual(set(descriptor), {'paletteRemap', 'images'})
        self.assertEqual(descriptor['images'], [
            {'group': 0, 'frame': index, 'defFile': 'SP11_.DEF', 'defGroup': 0, 'defFrame': index}
            for index in range(20)])
        self.assertEqual(row['reservedPaletteIndicesPreserved'], list(range(8)))
        self.assertEqual(row['originalExternalResource'], 'SP11_.DEF')
        self.assertEqual(row['originalExternalResourceSha256'],
                         'd17a6011caa515d5a4b9f04ef635e1a4117edf5bd38ede2b83f354e122d5ba35')
        for index, color in descriptor['paletteRemap'].items():
            self.assertTrue(8 <= int(index) <= 255)
            self.assertEqual(len(color), 3)
            self.assertTrue(all(type(channel) is int and 0 <= channel <= 255 for channel in color))
        self.assertIn('in-game rendered acceptance pending', row['approval'])

    def test_header_baseline_and_status_only_graphics(self):
        for index, wave in enumerate(self.waves):
            # Interface-button wave was selected after the faction rebindings;
            # earlier wave anchors must still see their exact original IDs.
            current_header = wave.get('orderEntryButtons') or wave.get('nativeEffectAliases')
            header = self.header if current_header else header_before_reworked_perks(self.header)
            if not wave.get('heroInterfaceButtons') and not current_header:
                header = header_before_faction_rebindings(header)
            for later in self.waves[index:]:
                for row in later['perks']:
                    line = '\t\t{"' + row['id'] + '", "' + row['icon44'] + '"},\n'
                    self.assertEqual(header.count(line), 1); header = header.replace(line, '')
            self.assertIn(wave['baselineHeaderSha256'], {
                hashlib.sha256(form.encode()).hexdigest() for form in perk_header_history_forms(header)})
            historical = config_at_revision(self.config, importer.spells.CONFIG, self.configProofs,
                wave_config_revision(wave, importer.spells.CONFIG))
            for later in reversed(self.waves[index + 1:]):
                for status in later['statuses']:
                    historical[status['id'].split(':')[1]]['graphics'].update(status['previousBindings'])
            self.assertEqual(importer.non_status_config_digest(historical, wave['statuses']), wave['nonStatusConfigSha256'])
            for status in wave['statuses']:
                self.assertFalse(status['castable']); self.assertIn(status['id'], {'new-horizons:arcaneBreach', 'new-horizons:lucidity'})
                self.assertEqual(set(status['bindings']), {'iconEffect', 'iconImmune'})
                self.assertEqual(len(status['resources']), 1)
                for role, filename in status['bindings'].items():
                    self.assertEqual(self.config[status['id'].split(':')[1]]['graphics'][role], filename)
                    image = open_image('SPRITES/' + filename)
                    self.assertEqual(image.mode, 'RGBA'); self.assertEqual(image.size, (30, 30))
                    self.assertEqual(image.getchannel('A').getextrema(), (0, 255))
                    self.assertEqual(hashlib.sha256(read_resource('SPRITES/' + filename)).hexdigest(), status['native30Sha256'])
            for reused in wave['reusedBindings']:
                config = importer.spells.parse_jsonc((ROOT / reused['config']).read_text())
                config = config_at_revision(config, reused['config'], self.configProofs,
                    wave_config_revision(wave, reused['config']))
                historical_config = config_at_wave(config, self.waves, index, reused['config'])
                self.assertEqual(importer.non_status_config_digest(historical_config, [reused]), reused['nonGraphicsConfigSha256'])
                self.assertEqual(historical_config[reused['id'].split(':')[1]]['graphics']['iconScenarioBonus'], 'NH_regeneration_32.png')
                self.assertEqual(open_image('SPRITES/NH_regeneration_32.png').size, (32, 32))

    def test_sanitized_provisional_records_and_no_loose_images(self):
        text = json.dumps(self.waves)
        self.assertNotRegex(text, r'(?i)/home/|/users/|[a-z]:[\\/]|\\\\')
        inventory = list(csv.DictReader(io.StringIO((ROOT / importer.INVENTORY).read_text())))
        for wave in self.waves:
            for row in wave['perks'] + wave['statuses'] + wave.get('spells', []):
                self.assertIn('no Final' if row in wave['perks'] else 'not Final', row['approval'])
                current_id = WAVE_CURRENT_IDS.get(row['id'], row['id'])
                matches = [entry for entry in inventory if entry['Item / ID'].endswith('(' + current_id + ')')]
                self.assertEqual(len(matches), 1); self.assertEqual(matches[0]['Art'], 'Provisional')
            for name in wave['resources']:
                self.assertFalse((ROOT / 'Mods/new-horizons/Images' / name.removeprefix('SPRITES/')).exists())

    def test_optional_mass_spell_records_have_exact_roles_payloads_and_provenance(self):
        for index, wave in enumerate(self.waves):
            if 'spells' not in wave:
                continue # Historical format1 waves retain their original shape.
            records = wave['spells']
            self.assertEqual(len(records), 4)
            self.assertEqual({row['id'] for row in records},
                             {'new-horizons:' + key for key in importer.MASS_SPELLS})
            config = importer.spells.parse_jsonc((ROOT / importer.MASS_CONFIG).read_text())
            config = config_at_revision(config, importer.MASS_CONFIG, self.configProofs,
                wave_config_revision(wave, importer.MASS_CONFIG))
            config = config_at_wave(config, self.waves, index, importer.MASS_CONFIG)
            self.assertEqual(importer.non_status_config_digest(config, records), wave['massConfigNonGraphicsSha256'])
            for row in records:
                key = row['id'].split(':')[1]
                stem = 'NH_spell_' + importer.MASS_SPELLS[key].replace('-', '_')
                expected = {role: stem + '-' + str(size) + '.png'
                            for role, size in importer.spells.ROLES.items()}
                self.assertEqual(row['config'], importer.MASS_CONFIG)
                self.assertEqual(row['bindings'], expected)
                self.assertEqual(set(row['previousBindings']), set(expected))
                self.assertEqual(config[key]['graphics'], expected)
                self.assertEqual(set(row['exports']), set(expected.values()))
                self.assertEqual(set(row['resources']), {'SPRITES/' + name for name in expected.values()})
                for filename, metadata in row['exports'].items():
                    size = metadata['size']; self.assertIn(size, (44, 32, 30))
                    resource = 'SPRITES/' + filename
                    self.assertEqual(hashlib.sha256(read_resource(resource)).hexdigest(), metadata['sha256'])
                    image = open_image(resource)
                    self.assertEqual(image.size, (size, size)); self.assertEqual(image.mode, 'RGBA')
                    self.assertEqual(image.getchannel('A').getextrema(), (0, 255))
                for field in ('handoffSha256', 'selectionHandoffSha256', 'masterSha256', 'promptSha256',
                              'reviewSha256', 'comparisonSha256', 'exportManifestSha256'):
                    self.assertRegex(row[field], r'^[0-9a-f]{64}$')


    def test_optional_specialty_records_have_exact_native_frame0_resources(self):
        manifest = {row['resource']: row for row in self.manifest['entries']}
        for wave in self.waves:
            if 'specialties' not in wave:
                continue  # Earlier waves retain their original format and checks.
            records = wave['specialties']
            self.assertIsInstance(records, list)
            self.assertTrue(records)
            self.assertEqual(len({row['id'] for row in records}), len(records))
            resources = [name for row in records for name in row['resources']]
            self.assertEqual(len(resources), len(records) * 4)
            self.assertEqual(len(set(resources)), len(resources))
            for row in records:
                family = specialty_family(row, manifest)
                self.assertRegex(row['id'], r'^[A-Za-z0-9_.-]+:[A-Za-z0-9_.-]+$')
                self.assertRegex(row['identity'], r'^[A-Za-z][A-Za-z0-9_]*$')
                for field in ('version', 'origin', 'selection', 'approval'):
                    self.assertIsInstance(row[field], str)
                    self.assertTrue(row[field].strip())
                self.assertRegex(row['selectionEvidenceSha256'], r'^[0-9a-f]{64}$')
                stem = 'newHorizons/heroSpecialties/' + row['identity']
                expected = {'SPRITES/' + stem + '/' + filename
                            for size in (44, 32)
                            for filename in (str(size) + '.json', 'icon-' + str(size) + '.png')}
                self.assertEqual(set(row['resources']), expected)
                self.assertEqual(len(row['resources']), 4)
                for size in (44, 32):
                    descriptor = 'SPRITES/' + stem + '/' + str(size) + '.json'
                    image_name = stem + '/icon-' + str(size) + '.png'
                    image_resource = 'SPRITES/' + image_name
                    self.assertEqual(row['normal' + str(size)], stem + '/' + str(size))
                    self.assertEqual(json.loads(read_resource(descriptor)),
                                     {'images': [{'group': 0, 'frame': 0, 'file': image_name}]})
                    selected_hash = row['native' + str(size) + 'Sha256']
                    self.assertRegex(selected_hash, r'^[0-9a-f]{64}$')
                    self.assertEqual(hashlib.sha256(read_resource(image_resource)).hexdigest(), selected_hash)
                    self.assertEqual(manifest[image_resource]['sha256'], selected_hash)
                    image = open_image(image_resource)
                    self.assertEqual(image.mode, 'RGBA')
                    self.assertEqual(image.size, (size, size))
                    self.assertFalse(image.info)
                    alpha = image.getchannel('A')
                    self.assertEqual(alpha.getextrema(), (0, 255))
                    edges = [(0, 0, size, 2), (0, size - 2, size, size),
                             (0, 0, 2, size), (size - 2, 0, size, size)]
                    for box in edges:
                        self.assertEqual(alpha.crop(box).getextrema(), (0, 0))
                for name in expected:
                    metadata = manifest[name]
                    self.assertEqual(metadata['family'], family)
                    self.assertEqual(metadata['selection'], row['selection'])
                    self.assertTrue(metadata['approval'].strip())
                    self.assertTrue(metadata['origin'])
                    self.assertFalse((ROOT / 'Mods/new-horizons/Images' / name.removeprefix('SPRITES/')).exists())

    def test_selected_specialty_foreground_v2_cohort_keeps_exact_three_heroes(self):
        token = 'selected-provisional-standalone-specialty-foreground-v2-2026-10-21'
        records = [row for wave in self.waves for row in wave.get('specialties', [])
                   if row['selection'] == token]
        expected = {
            'core:serena': ('SerenaArcanePreparation',
                '39dd9bfb0f948987fe7a6854df7d05829a3484134155d3c3ec8afb672d2c1725',
                '86df8efb9b5e8f591ec547cb816c6d32f0a5f14749c30a5d188090d13c5d39fb'),
            'core:gem': ('GemRestorativeSorceress',
                'ac0e369fd6cce534ee04cf5b7ffffcd2fd99df3f0ceaa369367dcb30b3f37b70',
                '6f20a82f8b550c96a64e02a8cac0d952df74b3b6d2c41a710d13f7f826293c5d'),
            'core:daremyth': ('DaremythFortunesChild',
                'ad79d50817249c447f723228ca402a7c6e6031584023141cc1f1700b570c74ef',
                'b203c85f331ad7adf62e3fd4cb3ce1efa10d116092f287fcdd848f79e9ef1648')}
        self.assertEqual(len(records), 3)
        self.assertEqual({row['id'] for row in records}, set(expected))
        for row in records:
            identity, hash44, hash32 = expected[row['id']]
            self.assertEqual(row['version'], 'transparent-foreground-v2-20261021')
            self.assertIn('not Final', row['approval'])
            self.assertIn('provisional', row['selection'])
            self.assertEqual((row['identity'], row['native44Sha256'], row['native32Sha256']),
                             (identity, hash44, hash32))
            manifest = {entry['resource']: entry for entry in self.manifest['entries']}
            for name in row['resources']:
                self.assertIn('no Final', manifest[name]['approval'])


    def test_selected_monere_gerwulf_v1_cohort_keeps_exact_two_heroes(self):
        token = 'selected-provisional-standalone-specialty-foreground-v1-monere-gerwulf-2026-10-21'
        records = [row for wave in self.waves for row in wave.get('specialties', [])
                   if row['selection'] == token]
        expected = {
            'core:monere': ('MonereWispLine',
                '0d822c83a3fcf7045518e6a5dc209f5a34276713aed59f61a0098dabbc7115fe',
                '1a4251a7908663db4112ed3a18e5a4a3625803fcbc5a6d0e447da92f404b07ba'),
            'core:gerwulf': ('GerwulfBallistaLine',
                '468a04d94e11cdac18642cb865b24c8eab282affb57a05c4a1d8e3cf04f77270',
                '35ce0b7ca1442dc4fa29fdc5b8986afdc252a79d91d934845d5ccba1cf2ec275')}
        self.assertEqual(len(records), 2)
        self.assertEqual({row['id'] for row in records}, set(expected))
        for row in records:
            self.assertEqual(row['version'], 'transparent-foreground-v1-20261021')
            self.assertEqual((row['identity'], row['native44Sha256'], row['native32Sha256']),
                             expected[row['id']])
            self.assertIn('not Final', row['approval'])
            self.assertIn('provisional', row['selection'])



    def test_selected_andra_thorgrim_v2_cohort_keeps_exact_two_heroes(self):
        token = 'selected-provisional-standalone-specialty-foreground-v2-andra-thorgrim-2026-10-22'
        records = [row for wave in self.waves for row in wave.get('specialties', [])
                   if row['selection'] == token]
        expected = {
            'core:andra': ('AndraBottomlessMind',
                'fd69a288f8580f780a2cd868e57ee15ff6faafcfceb9adcfd26cddb21b755dd0',
                '3fae452c251c3ac3ddd274f4d6b7a6ebf57d4ea1e2775f481c1a864ebe8cafcf'),
            'core:thorgrim': ('ThorgrimMasterSpellward',
                '5a00b23154994b4c2ab45ff25ad9ac6df52767091b8f7c9915f304f67351bdbf',
                '416307bdfac22bcdb4f4457150ef743ac02b93f18d2eac1fcc4ca993f06df36a')}
        self.assertEqual(len(records), 2)
        self.assertEqual({row['id'] for row in records}, set(expected))
        for row in records:
            self.assertEqual(row['version'], 'transparent-foreground-v2-20261021')
            self.assertEqual((row['identity'], row['native44Sha256'], row['native32Sha256']),
                             expected[row['id']])
            self.assertIn('not Final', row['approval'])
            self.assertIn('provisional', row['selection'])



    def test_selected_pasis_christian_v1_cohort_keeps_exact_two_heroes(self):
        token = 'selected-provisional-standalone-specialty-foreground-v1-pasis-christian-2026-10-22'
        records = [row for wave in self.waves for row in wave.get('specialties', [])
                   if row['selection'] == token]
        expected = {
            'core:pasis': ('PasisWispLine',
                '0162eedc1f388defb5a7226e1a54f46cacff65c4051c0c2fa764e04ad2fd0d2c',
                'a1b06870343baefff1ea13b27cab052d99029b387c48f1137a7fafce0f4b3027'),
            'core:christian': ('ChristianBallistaLine',
                '8d05ccdacc28e031696bf220f9adb2665469584944f206cfb26ca8011983f40a',
                'b35265b5ac8abdf622aecee9d8037bf08f408c13a8340fa241ce93b82bdb48e8')}
        self.assertEqual(len(records), 2)
        self.assertEqual({row['id'] for row in records}, set(expected))
        for row in records:
            self.assertEqual(row['version'], 'transparent-foreground-v1-20261021')
            self.assertEqual((row['identity'], row['native44Sha256'], row['native32Sha256']),
                             expected[row['id']])
            self.assertIn('not Final', row['approval'])
            self.assertIn('provisional', row['selection'])
        by_id = {row['id']: row for row in records}
        self.assertIn('project-wisp-subject-reference', by_id['core:pasis']['origin'])
        self.assertEqual(by_id['core:pasis']['projectReferenceSha256'],
                         '52f21093935c3b0a3a08cbbf9e3874c5a2038542aa8329f8cb50b0e38a746c21')
        self.assertIn('text-only', by_id['core:christian']['origin'])
        self.assertIn('own-image-haze-removal-correction', by_id['core:christian']['origin'])


if __name__ == '__main__':
    unittest.main()
