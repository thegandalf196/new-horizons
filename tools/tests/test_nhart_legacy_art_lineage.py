"""Actual missing-lineage bytes plus finite metadata/payload corruption controls."""
import copy
import json
from pathlib import Path
import unittest
from .nhart_legacy_art_lineage import validate_art_roles, legacy_specialty_family
from .nhart_test_resources import read_resource

ROOT = Path(__file__).resolve().parents[2]


class LegacyArtLineageTest(unittest.TestCase):
    def setUp(self):
        self.waves = json.loads((ROOT / 'assets/new-horizons/runtime-art-additions.json').read_text())['waves']
        entries = json.loads((ROOT / 'assets/new-horizons/runtime-art-manifest.json').read_text())['entries']
        self.manifest = {row['resource']:row for row in entries}
        self.roles = [row for wave in self.waves for row in wave.get('artRoles', [])]
        self.specialties = [row for wave in self.waves for row in wave.get('specialties', []) if 'sourceEntries' in row and 'provenanceCohort' not in row]

    def test_exact_six_roles_and_eleven_specialty_records_validate_actual_selected_bytes(self):
        self.assertEqual(len(self.roles), 6)
        self.assertEqual(len(self.specialties), 11)
        for wave in self.waves:
            validate_art_roles(wave, self.manifest, read_resource)
        for row in self.specialties:
            self.assertEqual(legacy_specialty_family(row,self.manifest),row['family'])

    def test_provenance_unknown_roles_duplicates_and_family_relabeling_fail(self):
        row=self.roles[0]
        for field, value in [('origin','invented'),('approval','Final'),('role','unknown'),
                             ('renderedAcceptance','Passed'),('selectionEvidenceSha256','f'*64)]:
            changed=copy.deepcopy(row);changed[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):
                validate_art_roles({'artRoles':[changed]},self.manifest,read_resource)
        with self.assertRaises(ValueError):validate_art_roles({'artRoles':[row,row]},self.manifest,read_resource)
        changed=copy.deepcopy(self.specialties[0]);changed['family']='builtin-skills-perks-orders-and-ui'
        with self.assertRaises(ValueError):legacy_specialty_family(changed,self.manifest)
        with self.assertRaises(ValueError):legacy_specialty_family({'family':'hero-specialty-icons'},self.manifest)

    def test_payload_and_original_manifest_mutation_cannot_be_hidden_by_lineage_metadata(self):
        for row in self.roles:
            target=row['resources'][0]
            def corrupted(name):
                payload=read_resource(name)
                return payload[:-1]+bytes([payload[-1]^1]) if name==target else payload
            with self.subTest(role=row['role']),self.assertRaises(ValueError):
                validate_art_roles({'artRoles':[row]},self.manifest,corrupted)
        changed=copy.deepcopy(self.manifest);changed[self.roles[0]['resources'][0]]['origin']='relabeled'
        with self.assertRaises(ValueError):validate_art_roles({'artRoles':[self.roles[0]]},changed,read_resource)
