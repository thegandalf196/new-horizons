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
local function isPhantomUnitCaster(mechanics)
	local caster = mechanics:getUnitCaster()
	return caster ~= nil and caster:getPhantomInitialIntegrity() > 0
end

function Script:getHealLevel()
	return HEAL_LEVEL_FROM_STRING[self.healLevel] or ENUM.HealLevel.heal
end
function Script:getEffectiveHealLevel(mechanics)
	local level = self:getHealLevel()
	if isPhantomUnitCaster(mechanics) and level ~= ENUM.HealLevel.heal then
		-- Phantom casters may mend living troops, but cannot restore dead creatures
		-- or increase a stack beyond its ordinary maximum health.
		return ENUM.HealLevel.heal
	end
	return level
end
function Script:getHealPower()
	return HEAL_POWER_FROM_STRING[self.healPower] or ENUM.HealPower.permanent
end
function Script:getEffectiveHealPower(mechanics)
	if isPhantomUnitCaster(mechanics) and self:getHealLevel() ~= ENUM.HealLevel.heal then
		-- The reduced Heal level cannot be paired with one-battle resurrection power.
		return ENUM.HealPower.permanent
	end
	return self:getHealPower()
end
function Script:getMinFullUnits()
	return self.minFullUnits or 0
end
function Script:getEffectiveMinFullUnits(mechanics)
	if isPhantomUnitCaster(mechanics) and self:getHealLevel() ~= ENUM.HealLevel.heal then
		-- A minimum full-resurrection threshold must not prevent ordinary healing.
		return 0
	end
	return self:getMinFullUnits()
end

--- Only injured units are valid; resurrect/overheal levels also accept dead units.
function Script:isValidTarget(mechanics, unit)
	local level = self:getEffectiveHealLevel(mechanics)
	local allowDead = level ~= ENUM.HealLevel.heal
	if not unit:isValidTarget(allowDead) then return false end

	local injuries = unit:getTotalHealth() - unit:getAvailableHealth()
	if level == ENUM.HealLevel.resurrect then
		injuries = injuries - unit:getUnusableRemains() * unit:getMaxHealth()
	end
	if injuries <= 0 then return false end

	local mfu = self:getEffectiveMinFullUnits(mechanics)
	if mfu > 0 then
		local hpGained = math.min(mechanics:applySpellBonus(mechanics:getEffectValue(), unit), injuries)
		if hpGained < mfu * unit:getMaxHealth() then return false end
	end

	if unit:isDead() then
		local hexes = unit:getHexes()
		for i = 1, hexes:size() do
			local hex = hexes:at(i)
			local blockers = mechanics:getBattle():getUnitsIf(function(other)
				return other ~= unit and other:isValidTarget(false) and other:coversPos(hex)
			end)
			if #blockers > 0 then return false end
		end
	end

	return true
end

--- Returns HP and unit count change for hover tooltip.
function Script:getHealthChange(mechanics, spellTarget)
	local result = { hpDelta = 0, unitsDelta = 0 }
	local phantomCaster = isPhantomUnitCaster(mechanics)
	local healLevel = self:getEffectiveHealLevel(mechanics)
	for _, dest in ipairs(spellTarget) do
		local unit = dest.unit
		if unit and not (phantomCaster and unit:isDead()) then
			local copy = unit:copy()
			local healedHP, resurrected = copy:heal(mechanics:applySpellBonus(mechanics:getEffectValue(), unit),
				healLevel, self:getEffectiveHealPower(mechanics))
			result.hpDelta   = result.hpDelta   + healedHP
			result.unitsDelta = result.unitsDelta + resurrected
			result.unitType  = unit:getCreature()
		end
	end
	return result
end

--- Heals each target unit and emits battle log messages.
function Script:apply(mechanics, server, target)
	local battle       = mechanics:getBattle()
	local isUnitCaster = mechanics:getHeroCaster() == nil
	local phantomCaster = isPhantomUnitCaster(mechanics)
	local healLevel = self:getEffectiveHealLevel(mechanics)

	for _, dest in ipairs(target) do
		local unit = dest.unit
		if unit and not (phantomCaster and unit:isDead()) then
			local healedHP, resurrected = server:healUnit(
				battle, unit, mechanics:applySpellBonus(mechanics:getEffectValue(), unit), healLevel, self:getEffectiveHealPower(mechanics))

			if resurrected > 0 then
				local textID = resurrected == 1 and "core.genrltxt.117" or "core.genrltxt.116"
				local nameTextID = unit:getCreature():getNameTextID(unit:getCount())
				server:appendLog(battle, {
					append         = { textID },
					replaceStrings = { nameTextID },
					replaceNumbers = { resurrected }
				})
			elseif healedHP > 0 and isUnitCaster then
				local casterUnit = mechanics:getUnitCaster()
				server:appendLog(battle, {
					append         = { "core.genrltxt.414" },
					replaceStrings = {
						casterUnit:getCreature():getNameTextID(1),
						unit:getCreature():getNameTextID(1)
					},
					replaceNumbers = { healedHP }
				})
			end
		end
	end
end

return Script
