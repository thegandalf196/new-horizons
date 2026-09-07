-- Isolated Lua boundary controls; actual native serialization/AI are separate gates.
Base = {
    declareBonus = function() end,
    hasBonusOfType = function(_, bonuses) return bonuses.siege end,
    getBaseDamageSingle = function() return 2, 3 end
}
ENUM = {BonusSource = {artifact = 1, heroBaseSkill = 2}}
local script = dofile('scripts/damage/siegeWeapon.lua')
local calls = 0
local attacker = {
    getBonusesValue = function(_, query)
        calls = calls + 1
        return query.sourceType == 1 and 2 or 3
    end,
    isTurret = function() return false end
}
local function check(info, expectedMin, expectedMax, expectedCalls)
    calls = 0
    local low, high = script:getBaseDamageSingle(info)
    assert(low == expectedMin and high == expectedMax)
    assert(calls == expectedCalls)
end
local info = {attacker = attacker, attackerBonuses = {siege = true}}
check(info, 12, 18, 2) -- absent field stays legacy
info.siegeSkillMultiplier = 0
check(info, 12, 18, 2)
for _, multiplier in ipairs({1, 2, 4, 100}) do
    info.siegeSkillMultiplier = multiplier
    check(info, 2 * multiplier, 3 * multiplier, 0)
end
info.attackerBonuses.siege = false
check(info, 2, 3, 0)
info.attackerBonuses.siege = true
attacker.isTurret = function() return true end
check(info, 2, 3, 0)
print('PASS: trained siege multiplier, legacy attack, non-siege and turret boundaries')
