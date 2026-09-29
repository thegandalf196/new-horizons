local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local SPELL_ID = "new-horizons:plague"
local STATUS_TRIGGER = "core:plagueStatus"
local STATUS_DURATION = 3
local BASE_DAMAGE = 25
local POWER_NUMERATOR = 8
local POWER_DENOMINATOR = 10
local BASIS_POINTS_PER_WHOLE = 10000

local function rawTickDamage(mechanics)
	-- getEffectPower is raw New Horizons Spell Power (including the Inferno
	-- defending-siege +20); do not divide by the hero primary-growth powerDivisor.
	local rawSpellPower = math.max(0, mechanics:getEffectPower())
	local coefficient = mechanics:getSpellPowerCoefficientBasisPoints()
	return BASE_DAMAGE + math.floor(POWER_NUMERATOR * rawSpellPower * coefficient
		/ (POWER_DENOMINATOR * BASIS_POINTS_PER_WHOLE))
end

function Script:apply(mechanics, server, target)
	local battle = mechanics:getBattle()
	local spellKey = mechanics:getSpell():getJsonKey()
	local damage = rawTickDamage(mechanics)
	local duration = mechanics:adjustEffectDuration(STATUS_DURATION)
	local casterSide = mechanics:getCasterSide()
	local casterUnits = battle:getUnitsIf(function(candidate)
		return candidate:getSide() == casterSide
	end)
	local sourceUnitId = casterUnits[1] and casterUnits[1]:unitID() or -1

	for _, destination in ipairs(target) do
		local unit = destination.unit
		if unit and unit:isAlive() and self:isReceptive(mechanics, unit) then
			local previous = unit:getBonuses({
				type = "COMBAT_EVENT_TRIGGER",
				subtype = STATUS_TRIGGER
			}):filter(function(bonus)
				return bonus:getSource() == ENUM.BonusSource.spellEffect
					and bonus:getSourceID() == spellKey
			end)
			if previous:size() > 0 then
				server:removeUnitBonuses(battle, unit, previous)
			end

			server:addUnitBonus(battle, unit, {
				type = "COMBAT_EVENT_TRIGGER",
				subtype = STATUS_TRIGGER,
				val = damage,
				duration = ENUM.BonusDuration.nTurns,
				turns = duration,
				sourceType = ENUM.BonusSource.spellEffect,
				sourceID = spellKey,
				stacking = spellKey,
				addInfo = {
					casterSide = casterSide,
					spreadAttempts = 0,
					lastProcessedRound = mechanics:getBattleRound() - 1,
					sourceUnitId = sourceUnitId
				},
				description = "New Horizons: Plague"
			}, false)
		end
	end
end

return Script
