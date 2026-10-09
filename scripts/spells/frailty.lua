local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local DEFENSE_BASE_BONUS_FILTER = {
	type = "PRIMARY_SKILL",
	subtype = "defence",
	sourceType = ENUM.BonusSource.creatureAbility
}
local DEFENSE_LOSS_BONUS_FILTER = {
	type = "PRIMARY_SKILL",
	subtype = "defence",
	sourceType = ENUM.BonusSource.spellEffect
}

local CUMULATIVE_CAP_BASIS_POINTS = 6000
local BASIS_POINTS_PER_WHOLE = 10000

local function baseDefense(unit)
	-- CCreature::getBaseDefense() is the sum of CREATURE_ABILITY-sourced Defense
	-- bonuses. Query that same source on the live stack so temporary buffs and
	-- other battle bonuses never become part of Frailty's base.
	return math.max(0, unit:getBonusesValue(DEFENSE_BASE_BONUS_FILTER))
end

local function accumulatedBasisPoints(unit, spellKey)
	local previous = unit:getBonuses(DEFENSE_LOSS_BONUS_FILTER):filter(function(bonus)
		return bonus:getSourceID() == spellKey
	end)
	if previous:size() == 0 then return 0, previous end
	return math.max(0, previous:getBonus(1):getParametersAsNumber()), previous
end

local function perCastBasisPoints(mechanics)
	-- Shared with detached AI: only the SP term gains the authored specialty.
	-- Fixed 10%, ordinary 20% cast cap and Withering Touch order are unchanged.
	return mechanics:getFrailtyDefenseLossBasisPoints()
end

function Script:apply(mechanics, server, target)
	local battle = mechanics:getBattle()
	local spellKey = mechanics:getSpell():getJsonKey()
	local castLoss = perCastBasisPoints(mechanics)

	for _, destination in ipairs(target) do
		local unit = destination.unit
		if unit and unit:isAlive() and self:isReceptive(mechanics, unit) then
			local previousTotal, previousBonuses = accumulatedBasisPoints(unit, spellKey)
			local total = math.min(CUMULATIVE_CAP_BASIS_POINTS, previousTotal + castLoss)
			local defenseLoss = math.floor(baseDefense(unit) * total / BASIS_POINTS_PER_WHOLE)

			if previousBonuses:size() > 0 then
				server:removeUnitBonuses(battle, unit, previousBonuses)
			end

			server:addUnitBonus(battle, unit, {
				type = "PRIMARY_SKILL",
				subtype = "defence",
				val = -defenseLoss,
				duration = ENUM.BonusDuration.oneBattle,
				sourceType = ENUM.BonusSource.spellEffect,
				sourceID = spellKey,
				stacking = spellKey,
				statusTags = {"DEBUFF"},
				statusIdentity = spellKey,
				addInfo = total
			}, false)
		end
	end
end

return Script
