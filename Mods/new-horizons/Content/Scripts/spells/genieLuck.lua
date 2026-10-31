-- Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt.
local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local BESTOW = "new-horizons:bestowLuck"
local TURN = "new-horizons:turnOfFate"
local FORTUNE = "core:fortune"
local MISFORTUNE = "core:misfortune"
local MARKER = "new-horizons:wheelOfFortune"

function Script:isValidTarget(mechanics, unit)
	local key = mechanics:getSpell():getJsonKey()
	local caster = mechanics:getUnitCaster()
	return (key == BESTOW or key == TURN) and caster ~= nil and caster:isAlive()
		and unit ~= nil and unit:isAlive() and unit:isValidTarget(false)
end

function Script:apply(mechanics, server, target)
	local key = mechanics:getSpell():getJsonKey()
	if key ~= BESTOW and key ~= TURN then return end
	local negative = key == TURN
	local family = negative and MISFORTUNE or FORTUNE
	local battle = mechanics:getBattle()
	for _, destination in ipairs(target) do
		local unit = destination.unit
		if self:isValidTarget(mechanics, unit) and self:isReceptive(mechanics, unit)
			and mechanics:ownerMatches(unit) then
			-- Canonical source IDs preserve ordinary opposing-spell cancellation.
			-- Remove both families, including other component types of Misfortune.
			local previous = unit:getBonuses({}):filter(function(bonus)
				if bonus:getSource() ~= ENUM.BonusSource.spellEffect then return false end
				local source = bonus:getSourceID()
				return source == FORTUNE or source == MISFORTUNE
			end)
			if previous:size() > 0 then server:removeUnitBonuses(battle, unit, previous) end
			server:addUnitBonus(battle, unit, {
				type = "LUCK",
				val = negative and -3 or 3,
				duration = ENUM.BonusDuration.nTurns,
				turns = 5,
				sourceType = ENUM.BonusSource.spellEffect,
				sourceID = family,
				stacking = MARKER,
				statusTags = negative and {"DEBUFF"} or nil,
				statusIdentity = negative and MISFORTUNE or nil,
			}, false)
		end
	end
end

return Script
