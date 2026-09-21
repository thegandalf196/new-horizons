-- Isolated Lua boundary controls; actual native serialization/AI are separate gates.
Base = {
    declareBonus = function() end,
    hasBonusOfType = function(_, bonuses) return bonuses.siege end,
    getBaseDamageSingle = function() return 2, 3 end,
    getBaseDamage = function() return 200, 300 end
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

-- New Horizons supplies an absolute per-machine output. It must not consult
-- the legacy hero-Attack path or approximate the formula as a percentage.
attacker.isTurret = function() return false end
info.siegeSkillMultiplier = 100
for _, siege in ipairs({0, 20, 40, 60}) do
    info.machineBaseDamage = 50 + 2 * siege
    local low, high = script:getBaseDamage(info)
    assert(low == info.machineBaseDamage and high == info.machineBaseDamage)
end

-- Without the Siege weapon marker, the canonical payload is ignored and the
-- ordinary damage-calculator base remains authoritative.
info.attackerBonuses.siege = false
info.machineBaseDamage = 170
local low, high = script:getBaseDamage(info)
assert(low == 200 and high == 300)
print('PASS: trained siege multiplier, legacy attack, non-siege and turret boundaries')
