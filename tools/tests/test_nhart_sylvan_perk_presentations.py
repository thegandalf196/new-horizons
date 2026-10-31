"""Strict metadata/source and payload mutation controls for captured Sylvan art."""
import copy
import hashlib
from io import BytesIO
from pathlib import Path
import unittest
from PIL import Image, ImageEnhance
from .nhart_perk_presentations import validate_perk_presentation, validate_perk_payload
from .nhart_sylvan_perk_presentations import (SYLVAN_CONDITIONS, SYLVAN_ADAPTER_SHA256,
    SYLVAN_ORIGIN, SYLVAN_SELECTION, SYLVAN_APPROVAL, source_bound_sylvan_icon, validate_sylvan_family)

ROOT = Path(__file__).resolve().parents[2]


class SylvanPerkPresentationTest(unittest.TestCase):
    def setUp(self):
        self.header = (ROOT / 'client/windows/NewHorizonsPerkIcons.h').read_text()
        self.adapter = (ROOT / 'client/windows/NewHorizonsCapturedPerkIcons.h').read_text()

    def row(self, condition):
        stable, name, stem, old = condition
        return dict(schemaVersion=2, capturedPolicy={'sylvanTacticsPolicy': 1},
            id=stable, capturedName=name, icon44=stem, historicalIcon44=old,
            alphaMaximum44=255, alphaMaximum32=255, native44Sha256='a'*64,
            native32Sha256='b'*64, masterSha256='c'*64, exportManifestSha256='d'*64,
            selectionEvidenceSha256='e'*64, resolverSourceSha256=SYLVAN_ADAPTER_SHA256,
            origin=SYLVAN_ORIGIN, selection=SYLVAN_SELECTION, approval=SYLVAN_APPROVAL,
            resources=['SPRITES/'+stem+s for s in ('.json','.png','_32.png','_normal.png',
                '_pressed.png','_disabled.png','_highlighted.png')])

    def test_all_ten_require_current_capture_and_exact_name(self):
        self.assertEqual(len(SYLVAN_CONDITIONS), 10)
        for condition in SYLVAN_CONDITIONS:
            row = self.row(condition)
            validate_perk_presentation(row, self.header, self.adapter)
            self.assertEqual(source_bound_sylvan_icon(self.header, self.adapter,
                row['id'], row['capturedName'], {}), row['historicalIcon44'])
        self.assertEqual(source_bound_sylvan_icon(self.header, self.adapter,
            'new-horizons:archery.crossfire', 'Crossfire', {'sylvanTacticsPolicy':1}), 'NH_perk_crossfire')

    def test_metadata_schema_policy_identity_provenance_and_resources_fail_closed(self):
        base = self.row(SYLVAN_CONDITIONS[0])
        mutations = [('schemaVersion', True), ('schemaVersion', 3), ('capturedPolicy', {}),
            ('capturedPolicy', {'sylvanTacticsPolicy':True}), ('capturedPolicy', {'sylvanTacticsPolicy':2}),
            ('capturedPolicy', {'sylvanTacticsPolicy':1, 'extra':1}), ('id','unknown:perk'),
            ('capturedName','Unknown'), ('icon44','NH_perk_crossfire'), ('historicalIcon44','NH_perk_neutral'),
            ('alphaMaximum44',254), ('alphaMaximum32',True), ('origin','/private/input'),
            ('selection','unselected'), ('approval','Final'), ('masterSha256','bad'),
            ('exportManifestSha256','bad'), ('selectionEvidenceSha256','bad'),
            ('resolverSourceSha256','f'*64), ('resources',base['resources'][:-1]),
            ('resources',base['resources'][:-1]+[base['resources'][0]])]
        for key, value in mutations:
            row=copy.deepcopy(base); row[key]=value
            with self.subTest(key=key,value=value), self.assertRaises(ValueError):
                validate_perk_presentation(row,self.header,self.adapter)
        for key in base:
            row=copy.deepcopy(base); del row[key]
            with self.subTest(missing=key), self.assertRaises(ValueError):
                validate_perk_presentation(row,self.header,self.adapter)
        row=copy.deepcopy(base); row['prompt']='private text'
        with self.assertRaises(ValueError): validate_perk_presentation(row,self.header,self.adapter)

    def test_source_policy_and_legacy_binding_tampering_is_rejected(self):
        row=self.row(SYLVAN_CONDITIONS[0])
        for adapter in (None, self.adapter+'\n', self.adapter.replace('usesSylvanTactics', 'usesPerkRules')):
            with self.assertRaises(ValueError): validate_perk_presentation(row,self.header,adapter)
        changed=self.header.replace('"NH_perk_perfect_moment"','"NH_other"')
        with self.assertRaises(ValueError): validate_perk_presentation(row,changed,self.adapter)
        for policy in (True, 0, 2, '1', None):
            with self.assertRaises(ValueError):
                source_bound_sylvan_icon(self.header,self.adapter,row['id'],row['capturedName'],{'sylvanTacticsPolicy':policy})

    def payload(self):
        import json
        row=self.row(SYLVAN_CONDITIONS[0]); stem='SPRITES/'+row['icon44']; data={}
        def encode(image):
            buf=BytesIO();image.save(buf,format='PNG');return buf.getvalue()
        normal=Image.new('RGBA',(44,44)); normal.paste((180,120,60,255),(4,4,40,40))
        small=Image.new('RGBA',(32,32));small.paste((180,120,60,255),(3,3,29,29))
        data[stem+'.png']=encode(normal);data[stem+'_32.png']=encode(small)
        states={'normal':normal,'pressed':ImageEnhance.Brightness(normal).enhance(.82),
            'disabled':ImageEnhance.Brightness(ImageEnhance.Color(normal).enhance(.15)).enhance(.65),
            'highlighted':ImageEnhance.Brightness(normal).enhance(1.13)}
        for key,image in states.items():data[stem+'_'+key+'.png']=encode(image)
        data[stem+'.json']=json.dumps({'images':[{'group':0,'frame':i,'file':row['icon44']+'_'+s+'.png'} for i,s in enumerate(states)]}).encode()
        for size in (44,32):row['native'+str(size)+'Sha256']=hashlib.sha256(data[stem+('_32' if size==32 else '')+'.png']).hexdigest()
        return row,data,encode

    def test_payload_native_descriptor_recipe_alpha_and_margin_controls(self):
        row,data,encode=self.payload(); validate_perk_payload(row,data.__getitem__)
        stem='SPRITES/'+row['icon44']
        for target in (stem+'.json', stem+'_pressed.png', stem+'_disabled.png',stem+'_highlighted.png',stem+'_32.png'):
            damaged=dict(data)
            if target.endswith('.json'):damaged[target]=b'{"images":[]}'
            else:
                im=Image.open(BytesIO(damaged[target])).convert('RGBA');im.putpixel((0,0),(255,255,255,255));damaged[target]=encode(im)
            with self.subTest(target=target),self.assertRaises(ValueError):validate_perk_payload(row,damaged.__getitem__)
        damaged=dict(data); damaged[stem+'_pressed.png']=data[stem+'_normal.png']
        with self.assertRaises(ValueError):validate_perk_payload(row,damaged.__getitem__)

    def test_family_requires_all_ten_distinct_current_records(self):
        rows=[self.row(condition) for condition in SYLVAN_CONDITIONS]
        validate_sylvan_family(rows,self.header,self.adapter)
        validate_sylvan_family([],self.header,None)
        for damaged in (rows[:-1], rows+[rows[0]], rows[:-1]+[rows[0]]):
            with self.assertRaises(ValueError):validate_sylvan_family(damaged,self.header,self.adapter)
