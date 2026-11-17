"""Finite structural source/metadata controls; native behavior is tested separately."""
import copy
import hashlib
from pathlib import Path
import unittest
from .nhart_source_transitions import read_historical_source
from .nhart_perk_presentations import (CONDITION, OVERLOAD_SUFFIX, CURRENT_OVERLOAD_SUFFIX,
    header_before_perk_presentation, source_bound_icon, validate_perk_presentation,
    WISDOM_REBINDINGS, header_before_wisdom_rebindings,
    WARCASTING_REBINDINGS, LEARNING_REBINDINGS, ESTATES_REBINDINGS,
    header_before_reworked_perks)

ROOT = Path(__file__).resolve().parents[2]


class PerkPresentationTest(unittest.TestCase):
    def setUp(self):
        self.header = read_historical_source('client/windows/NewHorizonsPerkIcons.h')
        stem = CONDITION[2]
        self.row = dict(id=CONDITION[0], capturedName=CONDITION[1], icon44=stem,
            historicalIcon44=CONDITION[3], alphaMaximum44=254, alphaMaximum32=253,
            native44Sha256='a' * 64, native32Sha256='b' * 64,
            selectionEvidenceSha256='c' * 64, origin='synthetic unit metadata',
            selection='provisional test metadata', approval='Provisional; no Final',
            resources=['SPRITES/' + stem + suffix for suffix in
                ('.json', '.png', '_32.png', '_normal.png', '_pressed.png', '_disabled.png', '_highlighted.png')])

    def test_exact_id_and_authored_name_select_new_presentation(self):
        validate_perk_presentation(self.row, self.header)
        self.assertEqual(source_bound_icon(self.header, CONDITION[0], CONDITION[1]), CONDITION[2])

    def test_historical_missing_wrong_case_and_translated_names_keep_old_shield(self):
        for name in ('Countermage', '', 'arcane ballistics', 'Translated Arcane Ballistics'):
            with self.subTest(name=name):
                self.assertEqual(source_bound_icon(self.header, CONDITION[0], name), CONDITION[3])
        self.assertEqual(source_bound_icon(self.header, 'new-horizons:unknown', CONDITION[1]), 'NH_perk_neutral')

    def test_wrong_metadata_cannot_claim_new_art_or_replace_historical_binding(self):
        for field, value in [('id', 'new-horizons:unknown'), ('capturedName', 'Countermage'),
                             ('icon44', 'NH_other'), ('historicalIcon44', CONDITION[2]),
                             ('alphaMaximum44', 255), ('alphaMaximum32', True)]:
            row = copy.deepcopy(self.row); row[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                validate_perk_presentation(row, self.header)

    def test_missing_extra_and_duplicate_resources_are_rejected(self):
        for names in (self.row['resources'][:-1], self.row['resources'] + ['SPRITES/extra.png'],
                      self.row['resources'][:-1] + [self.row['resources'][0]]):
            row = copy.deepcopy(self.row); row['resources'] = names
            with self.assertRaises(ValueError):
                validate_perk_presentation(row, self.header)

    def test_wisdom_alias_history_requires_exact_roles_and_preserves_unrelated_edits(self):
        historical = header_before_wisdom_rebindings(self.header)
        for old, (current, icon) in WISDOM_REBINDINGS.items():
            before = '\t\t{"' + old + '", "' + icon + '"},\n'
            after = '\t\t{"' + current + '", "' + icon + '"},\n'
            self.assertEqual(historical.count(before), 1)
            for changed in (self.header.replace(after, after.replace(icon, 'NH_wrong'), 1),
                            self.header.replace(after, after * 2, 1),
                            self.header + before):
                with self.subTest(old=old), self.assertRaisesRegex(ValueError, 'Wisdom'):
                    header_before_wisdom_rebindings(changed)
        changed = self.header.replace('NH_perk_neutral', 'NH_changed_neutral')
        self.assertEqual(header_before_wisdom_rebindings(changed),
                         historical.replace('NH_perk_neutral', 'NH_changed_neutral'))

    def assert_exact_rebinding_history(self, bindings):
        historical = header_before_reworked_perks(self.header)
        for old, (current, icon) in bindings.items():
            before = '\t\t{"' + old + '", "' + icon + '"},\n'
            after = '\t\t{"' + current + '", "' + icon + '"},\n'
            self.assertEqual(historical.count(before), 1)
            for changed in (self.header.replace(after, after.replace(icon, 'NH_wrong'), 1),
                            self.header.replace(after, after * 2, 1),
                            self.header.replace(after, '', 1), self.header + before):
                with self.subTest(old=old), self.assertRaisesRegex(ValueError, 'reworked'):
                    header_before_reworked_perks(changed)
        changed = self.header.replace('NH_perk_neutral', 'NH_changed_neutral')
        self.assertEqual(header_before_reworked_perks(changed),
                         historical.replace('NH_perk_neutral', 'NH_changed_neutral'))

    def test_warcasting_history_rejects_missing_duplicate_wrong_and_old_roles(self):
        self.assert_exact_rebinding_history(WARCASTING_REBINDINGS)

    def test_learning_history_rejects_missing_duplicate_wrong_and_old_roles(self):
        self.assert_exact_rebinding_history(LEARNING_REBINDINGS)

    def test_estates_history_rejects_missing_duplicate_wrong_and_old_roles(self):
        self.assert_exact_rebinding_history(ESTATES_REBINDINGS)

    def test_only_exact_known_overload_is_reversible_for_header_history(self):
        base = header_before_perk_presentation(self.header)
        self.assertEqual(self.header, base.replace('#include <string>\n', '#include <string>\n#include <string_view>\n') + OVERLOAD_SUFFIX)
        self.assertEqual(header_before_perk_presentation(base), base)
        for changed in (self.header.replace('capturedName == "Arcane Ballistics"', 'capturedName == "Countermage"'),
                        self.header + '\n', '#include <string_view>\n' + self.header):
            with self.assertRaises(ValueError):
                header_before_perk_presentation(changed)
        changed = self.header.replace('"NH_perk_countermage"', '"NH_other"')
        with self.assertRaises(ValueError):
            source_bound_icon(changed, CONDITION[0], CONDITION[1])

    def test_unrelated_old_header_edits_remain_visible_to_historical_hash_checks(self):
        base = header_before_perk_presentation(self.header)
        changed = self.header.replace('NH_perk_neutral', 'NH_changed_neutral')
        self.assertNotEqual(hashlib.sha256(header_before_perk_presentation(changed).encode()).digest(),
                            hashlib.sha256(base.encode()).digest())


class CurrentPerkLabelPresentationTest(unittest.TestCase):
    def test_current_header_accepts_only_two_exact_authored_labels(self):
        current = (ROOT / 'client/windows/NewHorizonsPerkIcons.h').read_text()
        base = header_before_perk_presentation(current)
        self.assertTrue(current.endswith(CURRENT_OVERLOAD_SUFFIX))
        self.assertEqual(header_before_perk_presentation(
            current[:-len(CURRENT_OVERLOAD_SUFFIX)] + OVERLOAD_SUFFIX), base)
        for label in ('Arcane Arsenal', 'Arcane Ballistics'):
            self.assertEqual(source_bound_icon(current, CONDITION[0], label), CONDITION[2])
        for label in ('Countermage', '', 'arcane arsenal', 'arcane ballistics',
                      'Translated Arcane Arsenal', 'Arcane Arsenal II'):
            self.assertEqual(source_bound_icon(current, CONDITION[0], label), CONDITION[3])
        for label in ('Arcane Arsenal', 'Arcane Ballistics'):
            self.assertEqual(source_bound_icon(current, 'unknown:perk', label), 'NH_perk_neutral')
        for changed in (current.replace('capturedName == "Arcane Arsenal"',
                                        'capturedName == "Unapproved Arsenal"'),
                        current.replace('NH_perk_arcane_ballistics', 'NH_unapproved_icon'),
                        current + '\n', current + CURRENT_OVERLOAD_SUFFIX):
            with self.assertRaises(ValueError):
                header_before_perk_presentation(changed)
        changed = current.replace('NH_perk_neutral', 'NH_changed_neutral')
        self.assertEqual(header_before_perk_presentation(changed),
                         base.replace('NH_perk_neutral', 'NH_changed_neutral'))
