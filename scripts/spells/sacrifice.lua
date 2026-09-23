local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local HEAL_LEVEL_FROM_STRING = {
	heal      = ENUM.HealLevel.heal,
	resurrect = ENUM.HealLevel.resurrect,
	overHeal  = ENUM.HealLevel.overheal,
}
local HEAL_POWER_FROM_STRING = {
	oneBattle = ENUM.HealPower.oneBattle,
	permanent = ENUM.HealPower.permanent,
}
local function isPhantom(unit)
	return unit:getPhantomInitialIntegrity() > 0
end
local function isPhantomUnitCaster(mechanics)
	local caster = mechanics:getUnitCaster()
	return caster ~= nil and isPhantom(caster)
end

function Script:getHealLevel()
	return HEAL_LEVEL_FROM_STRING[self.healLevel] or ENUM.HealLevel.resurrect
end
function Script:getHealPower()
	return HEAL_POWER_FROM_STRING[self.healPower] or ENUM.HealPower.permanent
end

--- Accept any unit (dead or alive) as a potential target.
function Script:isValidTarget(mechanics, unit)
	return not isPhantomUnitCaster(mechanics) and not isPhantom(unit) and unit:isValidTarget(true)
end

--- Enforce target type pair [CREATURE, CREATURE].
function Script:adjustTargetTypes(mechanics, types)
	if #types == 0 then return types end
	if types[1] ~= ENUM.AimType.creature then return {} end
	if #types == 1 then return { ENUM.AimType.creature, ENUM.AimType.creature } end
	if types[2] ~= ENUM.AimType.creature then return {} end
	return types
end

--- Require at least one dead unit AND one alive unit, both owner-matching.
function Script:applicableGeneral(mechanics, problem)
	if isPhantomUnitCaster(mechanics) then
		problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
		return false
	end

	local units = mechanics:getBattle():getUnitsIf(function(unit)
		return self:isValidTarget(mechanics, unit) and mechanics:isReceptive(unit) and mechanics:ownerMatches(unit)
	end)

	local hasDeadTarget  = false
	local hasAliveVictim = false
	for _, unit in ipairs(units) do
		if unit:isDead()  then hasDeadTarget  = true end
		if unit:isAlive() then hasAliveVictim = true end
		if hasDeadTarget and hasAliveVictim then break end
	end

	if not (hasDeadTarget and hasAliveVictim) then
		problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
		return false
	end
	return true
end

--- First target must be a dead unit; second must be an alive, receptive, owner-matching unit.
function Script:applicableTarget(mechanics, problem, target)
	if isPhantomUnitCaster(mechanics) then return false end
	if #target == 0 then return false end
	local deadUnit = target[1].unit
	if not deadUnit or deadUnit:isAlive() or not self:isValidTarget(mechanics, deadUnit) then return false end
	-- A one-target corpse prefix is valid for target enumeration. A supplied
	-- secondary target, however, must remain visible here even when invalid so
	-- it cannot collapse into that valid prefix.
	if #target == 1 then return true end
	if #target ~= 2 then return false end
	local victim = target[2].unit
	if not victim or not victim:isAlive() or not self:isValidTarget(mechanics, victim) then return false end
	if not mechanics:isReceptive(victim) then return false end
	if not mechanics:ownerMatches(victim) then return false end
	return true
end

--- Filter the dead target via the base, then append a live victim from aimPoint.
function Script:transformTarget(mechanics, aimPoint, spellTarget)
	if isPhantomUnitCaster(mechanics) then return {} end
	-- An explicit corpse must not alias another unit occupying the same hex.
	-- Let the base reject it normally; only hex-only input uses the old lookup.
	if #aimPoint >= 1 and aimPoint[1].unit then
		spellTarget = { aimPoint[1] }
	end
	local filtered = Base.transformTarget(self, mechanics, aimPoint, spellTarget)
	if #filtered == 0 then return {} end
	local result = { filtered[1] }
	if #aimPoint >= 2 then
		-- Keep an explicit secondary identity until applicableTarget validates it.
		-- Dropping a rejected victim here makes an invalid pair indistinguishable
		-- from the legitimate one-unit corpse prefix used by AI target enumeration.
		result[2] = aimPoint[2]
	end
	return result
end

function Script:calculateHealValue(mechanics, victim)
	local count = victim:getCount()
	local fixedPerCreature = victim:getMaxHealth() + mechanics:calculateRawEffectValue(0, 1)
	local scaledPowerForStack = mechanics:scaleSpellPowerComponent(
		mechanics:getEffectPower() * count, mechanics:getEffectPowerDivisor())
	return fixedPerCreature * count + scaledPowerForStack
end

--- Returns HP change preview.
function Script:getHealthChange(mechanics, spellTarget)
	if isPhantomUnitCaster(mechanics) then
		return { hpDelta = 0, unitsDelta = 0 }
	end
	if #spellTarget == 0 then
		return { hpDelta = 0, unitsDelta = 0 }
	end
	local unit = spellTarget[1].unit
	if not unit then
		return { hpDelta = 0, unitsDelta = 0 }
	end
	if isPhantom(unit) then
		return { hpDelta = 0, unitsDelta = 0 }
	end
	if not unit:isAlive() then
		-- dead target: show maximum possible resurrection
		local baseAmount = unit:getBaseAmount()
		local maxHP      = unit:getMaxHealth()
		return {
			hpDelta   = baseAmount * maxHP,
			unitsDelta = baseAmount,
			unitType   = unit:getCreature()
		}
	else
		-- alive unit shown in UI as sacrifice victim
		return {
			hpDelta   = self:calculateHealValue(mechanics, unit),
			unitsDelta = -unit:getCount(),
			unitType   = unit:getCreature()
		}
	end
end

--- Heal the dead target with the sacrifice value then remove the victim.
function Script:apply(mechanics, server, target)
	if isPhantomUnitCaster(mechanics) then return end
	if #target ~= 2 or not self:applicableTarget(mechanics, nil, target) then return end
	local deadTarget = target[1].unit
	local victim     = target[2].unit

	local healValue = self:calculateHealValue(mechanics, victim)
	local battle    = mechanics:getBattle()

	local _, resurrected = server:healUnit(
		battle, deadTarget, healValue,
		self:getHealLevel(), self:getHealPower())

	server:removeUnit(battle, victim)

	if resurrected > 0 then
		local textID = resurrected == 1 and "core.genrltxt.117" or "core.genrltxt.116"
		local nameTextID = deadTarget:getCreature():getNameTextID(deadTarget:getCount())
		server:appendLog(battle, {
			append         = { textID },
			replaceStrings = { nameTextID },
			replaceNumbers = { resurrected }
		})
	end
end

return Script
