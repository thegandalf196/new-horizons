local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local SPELL_KEY = "new-horizons:theChosen"

local function sourceBonuses(unit)
	return unit:getBonuses({sourceType = ENUM.BonusSource.spellEffect}):filter(function(bonus)
		return bonus:getSourceID() == SPELL_KEY
	end)
end

-- Stasis freezes spell effects, including their removal. Test this during target
-- validation (before spending the Hero Action/mana), and again in apply below.
local function canReplace(mechanics, unit)
	if unit:isTimeStopped() then return false end
	local blocked = mechanics:getBattle():getUnitsIf(function(previous)
		return previous:getSide() == unit:getSide() and previous:isTimeStopped()
			and sourceBonuses(previous):size() > 0
	end)
	return #blocked == 0
end

function Script:isValidTarget(mechanics, unit)
	return unit ~= nil and unit:isValidTarget(false) and unit:isLiving()
		and not unit:hasBonuses({type = "MECHANICAL"}) and mechanics:ownerMatches(unit)
		and canReplace(mechanics, unit)
end

function Script:apply(mechanics, server, target)
	-- The cast pipeline has already excluded resisted destinations. Never clear
	-- an existing recipient until a new destination has passed every effect gate.
	local destination = target[1]
	local unit = destination and destination.unit
	if not self:isValidTarget(mechanics, unit) or not self:isReceptive(mechanics, unit) then return end
	local turns = mechanics:getTheChosenDuration()
	local jousting = mechanics:getTheChosenJoustingPercent()
	if turns <= 0 or jousting <= 0 then return end
	local battle = mechanics:getBattle()
	-- getSide is the original army, independent of temporary allegiance/control.
	-- Include corpses so resurrection cannot restore a second old recipient.
	for _, previous in ipairs(battle:getUnitsIf(function(candidate)
		return candidate:getSide() == unit:getSide()
	end)) do
		local bonuses = sourceBonuses(previous)
		if bonuses:size() > 0 then server:removeUnitBonuses(battle, previous, bonuses) end
	end
	local abilities = {
		{type = "ADDITIONAL_ATTACK", val = 1},
		{type = "UNLIMITED_RETALIATIONS", val = 1},
		{type = "FLYING", val = 1},
		{type = "JOUSTING", val = jousting}
	}
	for _, bonus in ipairs(abilities) do
		bonus.duration = ENUM.BonusDuration.nTurns
		bonus.turns = turns
		bonus.valueType = "INDEPENDENT_MAX"
		bonus.sourceType = ENUM.BonusSource.spellEffect
		bonus.sourceID = SPELL_KEY
		server:addUnitBonus(battle, unit, bonus, false)
	end
end

return Script
