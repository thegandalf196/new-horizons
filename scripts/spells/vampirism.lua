local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local VAMPIRISM_SPELL = "new-horizons:vampirism"
local VAMPIRISM_STATUS = "core:vampirism"
local STATUS_DURATION_ROUNDS = 3

local function reject(problem, mechanics)
	if problem then
		problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
	end
	return false
end

local function validFriendlyTarget(self, mechanics, unit)
	return unit ~= nil and unit:isAlive() and unit:isValidTarget(false)
		and mechanics:ownerMatches(unit) and self:isReceptive(mechanics, unit)
end

function Script:applicableGeneral(mechanics, problem)
	if not mechanics:usesNewHorizonsMagicV3()
		or mechanics:getSpell():getJsonKey() ~= VAMPIRISM_SPELL then
		return reject(problem, mechanics)
	end

	local targets = mechanics:getBattle():getUnitsIf(function(unit)
		return validFriendlyTarget(self, mechanics, unit)
	end)
	if #targets == 0 then
		return reject(problem, mechanics)
	end
	return true
end

function Script:applicableTarget(mechanics, problem, target)
	if not mechanics:usesNewHorizonsMagicV3()
		or mechanics:getSpell():getJsonKey() ~= VAMPIRISM_SPELL
		or #target ~= 1 or not validFriendlyTarget(self, mechanics, target[1].unit) then
		return reject(problem, mechanics)
	end
	return true
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	if not mechanics:usesNewHorizonsMagicV3()
		or mechanics:getSpell():getJsonKey() ~= VAMPIRISM_SPELL
		or #aimPoint ~= 1 or not aimPoint[1].unit then
		return {}
	end
	return { aimPoint[1] }
end

local function healBasisPoints(mechanics)
	return mechanics:getVampirismHealBasisPoints()
end

function Script:apply(mechanics, server, target)
	if not self:applicableTarget(mechanics, nil, target) then return end

	local unit = target[1].unit
	local battle = mechanics:getBattle()
	local spellKey = mechanics:getSpell():getJsonKey()
	local previous = unit:getBonuses({
		type = "COMBAT_EVENT_TRIGGER",
		subtype = VAMPIRISM_STATUS
	}):filter(function(bonus)
		return bonus:getSource() == ENUM.BonusSource.spellEffect
			and bonus:getSourceID() == spellKey
	end)
	if previous:size() > 0 then
		server:removeUnitBonuses(battle, unit, previous)
	end

	server:addUnitBonus(battle, unit, {
		type = "COMBAT_EVENT_TRIGGER",
		subtype = VAMPIRISM_STATUS,
		val = healBasisPoints(mechanics),
		duration = ENUM.BonusDuration.nTurns,
		turns = STATUS_DURATION_ROUNDS + mechanics:getExtendSpellBonusRounds(),
		sourceType = ENUM.BonusSource.spellEffect,
		sourceID = spellKey,
		stacking = spellKey,
		description = "New Horizons: Vampirism"
	}, false)
end

return Script
