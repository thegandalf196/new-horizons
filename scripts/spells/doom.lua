local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local SPELL_ID = "new-horizons:doom"
local DURATION_ROUNDS = 3
local BASE_CRIPPLING_PERCENT = 35
local MAX_CRIPPLING_PERCENT = 60
local SPELL_POWER_TERM_NUMERATOR = 3
local SPELL_POWER_TERM_DIVISOR = 20
local MORALE_PENALTY = 3

local BONUS_TYPES = {
	"GENERAL_ATTACK_REDUCTION",
	"STACKS_SPEED",
	"STACKS_INITIATIVE",
	"MORALE"
}

local function reject(problem, mechanics)
	if problem then
		problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
	end
	return false
end

local function isEnemyTarget(mechanics, unit)
	return unit ~= nil and unit:isAlive() and unit:isValidTarget(false)
		and unit:getSide() ~= mechanics:getCasterSide()
end

local function penaltyPercent(mechanics)
	local rawSpellPower = math.max(0, mechanics:getEffectPower())
	local spellPowerTerm = mechanics:scaleSpellPowerComponentWithCoefficientBasisPoints(
		rawSpellPower * SPELL_POWER_TERM_NUMERATOR, SPELL_POWER_TERM_DIVISOR,
		mechanics:getSpellPowerCoefficientBasisPoints())
	return math.min(MAX_CRIPPLING_PERCENT, BASE_CRIPPLING_PERCENT + spellPowerTerm)
end

local function bonusesForSpell(unit, bonusType, spellKey)
	return unit:getBonuses({type = bonusType}):filter(function(bonus)
		return bonus:getSource() == ENUM.BonusSource.spellEffect
			and bonus:getSourceID() == spellKey
	end)
end

local function removePreviousDoom(battle, server, unit, spellKey)
	local previous = {}
	local refreshed = false
	for _, bonusType in ipairs(BONUS_TYPES) do
		local matches = bonusesForSpell(unit, bonusType, spellKey)
		if matches:size() > 0 then
			refreshed = true
			server:removeUnitBonuses(battle, unit, matches)
		end
	end
	return refreshed
end

function Script:isValidTarget(mechanics, unit)
	return isEnemyTarget(mechanics, unit)
end

function Script:applicableGeneral(mechanics, problem)
	if mechanics:getSpell():getJsonKey() ~= SPELL_ID or not mechanics:usesNewHorizonsMagicV3() then
		return reject(problem, mechanics)
	end
	local targets = mechanics:getBattle():getUnitsIf(function(unit)
		return isEnemyTarget(mechanics, unit) and self:isReceptive(mechanics, unit)
	end)
	if #targets == 0 then
		return reject(problem, mechanics)
	end
	return true
end

function Script:applicableTarget(mechanics, problem, target)
	if mechanics:getSpell():getJsonKey() ~= SPELL_ID or not mechanics:usesNewHorizonsMagicV3()
		or #target ~= 1 then
		return reject(problem, mechanics)
	end
	local unit = target[1].unit
	if not isEnemyTarget(mechanics, unit) or not self:isReceptive(mechanics, unit) then
		return reject(problem, mechanics)
	end
	return true
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	if mechanics:getSpell():getJsonKey() ~= SPELL_ID or not mechanics:usesNewHorizonsMagicV3()
		or #aimPoint ~= 1 or not aimPoint[1].unit then
		return {}
	end
	-- Doom is always one enemy stack, even if a legacy static spell definition
	-- describes an Expert mass range.
	return { aimPoint[1] }
end

function Script:apply(mechanics, server, target)
	if not self:applicableTarget(mechanics, nil, target) then
		return
	end

	local destination = target[1]
	local unit = destination.unit
	local battle = mechanics:getBattle()
	local spellKey = mechanics:getSpell():getJsonKey()
	local penalty = penaltyPercent(mechanics)
	local duration = mechanics:adjustEffectDuration(DURATION_ROUNDS)
	local refreshed = removePreviousDoom(battle, server, unit, spellKey)

	local function applyBonus(bonusType, value, valueType)
		server:addUnitBonus(battle, unit, {
			type = bonusType,
			val = value,
			valueType = valueType,
			duration = ENUM.BonusDuration.nTurns,
			turns = duration,
			sourceType = ENUM.BonusSource.spellEffect,
			sourceID = spellKey,
			stacking = spellKey,
			description = "New Horizons: Doom"
		}, false)
	end

	-- One attacker-side reduction handles both ordinary attacks and retaliation;
	-- there is no separate return-strike penalty to multiply onto the same hit.
	applyBonus("GENERAL_ATTACK_REDUCTION", penalty, "ADDITIVE_VALUE")
	-- Percent-to-all scales the resolved Speed once, including additive Speed
	-- bonuses. Movement range uses this Speed; it does not alter Defense.
	applyBonus("STACKS_SPEED", -penalty, "PERCENT_TO_ALL")
	-- Initiative normally falls back to Speed. Only explicit initiative bases
	-- need their own modifier, otherwise the Speed penalty would apply twice.
	if unit:getBonuses({type = "STACKS_INITIATIVE_BASE"}):size() > 0 then
		applyBonus("STACKS_INITIATIVE", -penalty, "ADDITIVE_VALUE")
	end
	applyBonus("MORALE", -MORALE_PENALTY, "ADDITIVE_VALUE")

	if server:describeChanges() then
		server:appendLog(battle, {
			append = { refreshed and "new-horizons.combat.doom.refreshed"
				or "new-horizons.combat.doom.applied" },
			replaceStrings = { unit:getCreature():getNameTextID(unit:getCount()) },
			replaceNumbers = { duration, penalty }
		})
	end
end

return Script
