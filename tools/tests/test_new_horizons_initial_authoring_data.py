# SPDX-License-Identifier: GPL-2.0-or-later
"""Initial captured typed-authoring data; not native or rendered acceptance."""
import copy
import unittest
from jsonschema import Draft4Validator
from test_new_horizons_content import load

SCALARS={'core:fiona':'packPursuitSpeed','core:clancy':'resistanceAuraPoints','core:pasis':'prismaticScreenReductionPercent'}
class InitialAuthoringDataTest(unittest.TestCase):
 def setUp(self):
  self.perks=load('config/newHorizonsPerks.json');self.heroes=load('config/newHorizonsHeroes.json')
  self.perk_schema=Draft4Validator(load('config/schemas/newHorizonsPerks.json'))
  self.hero_schema=Draft4Validator(load('config/schemas/newHorizonsHeroes.json'))
 def test_six_rank_defaults_are_captured_once_outside_effect(self):
  for sid,kind in [('new-horizons:luck','LUCK'),('new-horizons:discipline','MORALE')]:
   for level,rank in enumerate(['basic','advanced','expert'],1):
    row=self.perks['skills'][sid]['ranks'][rank]
    self.assertEqual(row['mechanics'],{'version':1,'operations':[{'operation':'bonus','source':'skillRank','target':'army','stage':'attachment','lifetime':'permanent','type':kind,'value':level,'valueMode':'BASE_NUMBER'}]})
    self.assertEqual(set(row['effect']),{'status','description'})
  self.assertEqual(len(self.perks['skills']),31)
  self.assertEqual(sum(len(s['perks'])for s in self.perks['skills'].values()),310)
 def test_two_timed_rows_have_only_native_slot_contracts(self):
  for sid,pid,operation,lifetime in [('new-horizons:lightMagic','new-horizons:lightMagic.sanctuaryKeeper','sanctuaryKeeper','parentStatus'),('new-horizons:chaosMagic','new-horizons:chaosMagic.mindbreaker','creatureSuppression','rounds')]:
   row=next(p for p in self.perks['skills'][sid]['perks']if p['id']==pid)
   self.assertEqual(row['mechanics'],{'version':1,'operations':[{'operation':operation,'source':'spellAddition','target':'spellRecipient','stage':'castApplication','lifetime':lifetime}]})
   changed=copy.deepcopy(self.perks)
   target=next(p for p in changed['skills'][sid]['perks']if p['id']==pid)
   target['mechanics']['operations'][0]['value']=2
   self.assertFalse(self.perk_schema.is_valid(changed))
 def test_three_prepared_heroes_preserve_defaults_with_one_numeric_authority(self):
  profile=self.heroes['additionalSpecialties']['preparedCreatureSpecialties']
  self.assertEqual(profile['version'],1);self.assertEqual(set(profile['heroes']),set(SCALARS))
  for hero,value,kind in [('core:fiona',5,'STACKS_SPEED'),('core:clancy',10,'SPELL_RESISTANCE_AURA'),('core:pasis',15,'SPELL_DAMAGE_REDUCTION')]:
   operations=profile['heroes'][hero]['mechanics']['operations'];self.assertEqual(len(operations),1)
   self.assertEqual((operations[0]['value'],operations[0]['type']),(value,kind))
   self.assertNotIn(SCALARS[hero],self.heroes['additionalSpecialties']['ordinaryCreatureSpecialties'])
   changed=copy.deepcopy(self.heroes);changed['additionalSpecialties']['ordinaryCreatureSpecialties'][SCALARS[hero]]=value
   self.assertFalse(self.hero_schema.is_valid(changed))
 def test_malformed_present_records_cannot_select_legacy_route(self):
  for value in [None,{},[],{'version':2,'operations':[]}]:
   changed=copy.deepcopy(self.heroes);changed['additionalSpecialties']['preparedCreatureSpecialties']['heroes']['core:fiona']['mechanics']=value
   self.assertFalse(self.hero_schema.is_valid(changed))
  changed=copy.deepcopy(self.heroes);del changed['additionalSpecialties']['preparedCreatureSpecialties']['heroes']['core:clancy']
  self.assertFalse(self.hero_schema.is_valid(changed))
 def test_initial_hero_contract_is_fixed_but_captured_amount_is_not_ignored(self):
  for hero in SCALARS:
   changed=copy.deepcopy(self.heroes);operation=changed['additionalSpecialties']['preparedCreatureSpecialties']['heroes'][hero]['mechanics']['operations'][0]
   operation['value']=17;self.hero_schema.validate(changed)
   operation['family']='core:archer';self.assertFalse(self.hero_schema.is_valid(changed))
   operation['value']=True;self.assertFalse(self.hero_schema.is_valid(changed))
 def test_module_mirrors_canonical_captured_defaults(self):
  module=load('Mods/new-horizons/mod.json')
  self.assertEqual(module['settings']['heroes']['newHorizons'],self.heroes)
  self.assertEqual(module['settings']['heroes']['newHorizonsPerks'],self.perks)
  self.perk_schema.validate(self.perks);self.hero_schema.validate(self.heroes)
if __name__=='__main__':unittest.main()
