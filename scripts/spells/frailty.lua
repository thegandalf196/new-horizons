local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local SHADOW_MAGIC_SKILL = "new-horizons:shadowMagic"
local WITHERING_TOUCH_PERK = "new-horizons:shadowMagic.witheringTouch"

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

local SPELL_POWER_BASIS_POINTS_PER_POINT = 5
local BASE_DEFENSE_LOSS_BASIS_POINTS = 1000
local PER_CAST_CAP_BASIS_POINTS = 2000
local WITHERING_TOUCH_BASIS_POINTS = 500
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
	-- The fixed 10% is not school-scaled. The shared helper applies the saved
	-- School/Spellcraft coefficient to the 0.05% per Spell Power term, then this
	-- cast's Warcasting and Empower bonuses.
	local powerTerm = mechanics:scaleSpellPowerComponentWithCoefficientBasisPoints(
		math.max(0, mechanics:getEffectPower()) * SPELL_POWER_BASIS_POINTS_PER_POINT,
		1,
		mechanics:getSpellPowerCoefficientBasisPoints())
	local loss = math.min(PER_CAST_CAP_BASIS_POINTS,
		BASE_DEFENSE_LOSS_BASIS_POINTS + powerTerm)

	local hero = mechanics:getHeroCaster()
	if hero ~= nil and hero:hasActivePerk(SHADOW_MAGIC_SKILL, WITHERING_TOUCH_PERK) then
		loss = loss + WITHERING_TOUCH_BASIS_POINTS
	end
	return loss
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
