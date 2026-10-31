#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Captured Sorcery schema and executable Lua policy checks; no gameplay claim."""
import copy
import ctypes
import ctypes.util
import unittest
from pathlib import Path

from test_new_horizons_content import load, magic_validators

ROOT = Path(__file__).resolve().parents[2]


class SorceryRevisionSchemaTest(unittest.TestCase):
    def test_revised_default_and_historical_absence_validate(self):
        validator = magic_validators()[2]
        rules = load('config/newHorizonsMagic.json')
        validator.validate(rules)
        self.assertEqual(rules['spells']['core:slow']['level'], 3)
        self.assertEqual(rules['spells']['new-horizons:massSlow']['costs'], [33] * 4)
        self.assertFalse(rules['spells']['core:haste']['ordinaryAcquisition'])
        self.assertNotEqual(rules['spells']['core:haste'].get('heroAccess'), False)
        del rules['spells']['new-horizons:focusMagic']['focusMagic']
        del rules['spells']['core:slow']['slow']
        del rules['spells']['new-horizons:summonLivingArmor']
        validator.validate(rules)

    def test_rejects_null_unknown_fields_and_bad_profile_domains(self):
        validator = magic_validators()[2]
        rules = load('config/newHorizonsMagic.json')
        for identity, clause in (('core:slow', 'slow'),
                                 ('new-horizons:focusMagic', 'focusMagic'),
                                 ('new-horizons:summonLivingArmor', 'livingArmor')):
            profile = rules['spells'][identity][clause]
            for invalid in (None, False, {}, {**profile, 'version': 2},
                            {**profile, 'extra': 1}):
                changed = copy.deepcopy(rules)
                changed['spells'][identity][clause] = invalid
                with self.subTest(identity=identity, invalid=invalid):
                    self.assertFalse(validator.is_valid(changed))


class SorceryRevisionLuaTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        library = ctypes.util.find_library('lua5.4')
        if not library:
            raise unittest.SkipTest('Lua 5.4 shared library unavailable; native integration still required')
        cls.lua = ctypes.CDLL(library)
        cls.lua.luaL_newstate.restype = ctypes.c_void_p
        cls.lua.luaL_openlibs.argtypes = [ctypes.c_void_p]
        cls.lua.luaL_loadstring.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        cls.lua.luaL_loadstring.restype = ctypes.c_int
        cls.lua.lua_pcallk.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                                     ctypes.c_int, ctypes.c_ssize_t, ctypes.c_void_p]
        cls.lua.lua_pcallk.restype = ctypes.c_int
        cls.lua.lua_tolstring.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
        cls.lua.lua_tolstring.restype = ctypes.c_char_p
        cls.lua.lua_close.argtypes = [ctypes.c_void_p]

    def run_lua(self, body):
        state = self.lua.luaL_newstate()
        self.assertTrue(state)
        try:
            self.lua.luaL_openlibs(state)
            source = 'local Revision = (function()\n' + (
                ROOT / 'scripts/spells/sorceryRevision.lua').read_text() + '\nend)()\n'
            source += '''
local revised = true
local mechanics = {
 slowAffectsSpeed = function() return revised end,
 usesNewHorizonsMagicV3 = function() return true end,
 isMassSlow = function() return false end,
 getVariantPowerPercent = function() return 60 end,
 scaleSlowVariantValue = function(_, value, percent)
  assert(percent == 60)
  local expected = {[-31] = -18, [-50] = -30}
  assert(expected[value] ~= nil)
  return expected[value]
 end
}
ENUM = {BonusSource = {spellEffect = 1}}
''' + body
            status = self.lua.luaL_loadstring(state, source.encode())
            if status == 0:
                status = self.lua.lua_pcallk(state, 0, 0, 0, 0, None)
            error = self.lua.lua_tolstring(state, -1, None) if status else b''
            self.assertEqual(status, 0, error.decode() if error else '')
        finally:
            self.lua.lua_close(state)

    def test_dual_stat_copy_preserves_payload_and_scales_both_once(self):
        self.run_lua('''
local buffer = {initiative = {type="STACKS_INITIATIVE", val=-31, turns=3,
 valueType="ADDITIVE_VALUE", sourceID="core:slow", statusIdentity="core:slow"}}
Revision.addSlowSpeed(mechanics, buffer)
assert(buffer.slowSpeed ~= buffer.initiative)
assert(buffer.slowSpeed.val == -31 and buffer.slowSpeed.turns == 3)
assert(buffer.slowSpeed.valueType == "PERCENT_TO_ALL")
assert(buffer.slowSpeed.statusIdentity == "core:slow")
Revision.scaleSlow(mechanics, buffer, "core:slow")
assert(buffer.initiative.val == -18 and buffer.slowSpeed.val == -18)
assert(buffer.initiative.turns == 3 and buffer.slowSpeed.turns == 3)
''')

    def test_historical_capture_stays_initiative_only_and_foreign_speed_untouched(self):
        self.run_lua('''
revised = false
local buffer = {initiative={type="STACKS_INITIATIVE", val=-50},
 foreignSpeed={type="STACKS_SPEED",val=-40}}
Revision.addSlowSpeed(mechanics, buffer)
assert(buffer.slowSpeed == nil)
Revision.scaleSlow(mechanics, buffer, "core:slow")
assert(buffer.initiative.val == -30 and buffer.foreignSpeed.val == -40)
''')

    def test_refresh_removes_both_family_markers_in_one_operation(self):
        self.run_lua('''
local function bonus(kind, source, id)
 return {getType=function() return kind end, getSource=function() return source end,
 getSourceID=function() return id end}
end
local values = {bonus("STACKS_INITIATIVE",1,"core:slow"),
 bonus("STACKS_SPEED",1,"core:slow"),bonus("STACKS_SPEED",1,"other"),
 bonus("STACKS_INITIATIVE",2,"core:slow")}
local list={filter=function(self,predicate)
 local selected={}
 for _,v in ipairs(values) do if predicate(v) then table.insert(selected,v) end end
 return {size=function() return #selected end, values=selected}
end}
local unit={getBonuses=function() return list end}
local calls=0
local server={removeUnitBonuses=function(self,battle,u,removed)
 calls=calls+1
 assert(u==unit and #removed.values==2)
end}
Revision.refreshSlow(mechanics,server,{},unit,"core:slow")
assert(calls==1)
revised=false
server.removeUnitBonuses=function(self,battle,u,removed)
 calls=calls+1
 assert(#removed.values==1 and removed.values[1]:getType()=="STACKS_INITIATIVE")
end
Revision.refreshSlow(mechanics,server,{},unit,"core:slow")
assert(calls==2)
''')


if __name__ == '__main__':
    unittest.main()
