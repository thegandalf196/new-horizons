-- Execute the real spell scripts with bounded callback doubles, not gameplay acceptance.
package.path = "scripts/?.lua;" .. package.path
package.preload["spells/spellEffect"] = function() return {} end
package.preload["spells/unitEffect"] = function() return {} end
ENUM = {
    HealLevel = {heal=0, resurrect=1, overheal=2},
    HealPower = {oneBattle=0, permanent=1},
    BattleSide = {attacker=0, defender=1},
    ObstacleType = {spellCreated=1, moat=2}
}
local power, divisor = 9, 5
local battle = {hasMoat=function() return true end}
local mechanics = {
    getEffectPower=function() return power end,
    getEffectPowerDivisor=function() return divisor end,
    calculateRawEffectValue=function(_, base, level) return base * 100 + level * 3 end,
    applySpecificSpellBonus=function(_, value) return value + 10 end,
    getBattle=function() return battle end,
    getCasterSide=function() return ENUM.BattleSide.attacker end,
    getEffectLevel=function() return 3 end,
    getSpell=function() return {} end,
    isMassive=function() return true end
}
local summon = require("spells/summon")
assert(summon:summonedEffectValue(mechanics) == 15, "scale the full 9*3 product, not power before multiplication")
divisor = 1
assert(summon:summonedEffectValue(mechanics) == 37, "legacy summon magnitude unchanged")
local sacrifice = require("spells/sacrifice")
local victim = {getMaxHealth=function() return 10 end, getCount=function() return 2 end}
assert(sacrifice:calculateHealValue(mechanics, victim) == 44, "legacy sacrifice unchanged")
divisor = 5
assert(sacrifice:calculateHealValue(mechanics, victim) == 29, "retain health/rank and fractional power until final floor")
local descriptors = {}
local server = {addObstacle=function(_, _, descriptor) descriptors[#descriptors+1] = descriptor end}
local hex = {}
local obstacle = require("spells/obstacle")
obstacle:apply(mechanics, server, {{hex=hex}})
local moat = setmetatable({moatHexes={{hex}}}, {__index=require("spells/moat")})
moat:apply(mechanics, server, {})
assert(#descriptors == 2)
for _, descriptor in ipairs(descriptors) do
    assert(descriptor.casterSpellPower == 9)
    assert(descriptor.casterPowerDivisor == 5, "latch divisor separately from raw power")
end
divisor = 1
obstacle:apply(mechanics, server, {{hex=hex}})
assert(descriptors[3].casterPowerDivisor == 1, "legacy/ordinary creature divisor remains one")
