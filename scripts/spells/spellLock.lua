local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local SPELL_KEY = "new-horizons:spellLock"
local SPELLBINDER_SKILL = "new-horizons:sorceryMagic"
local SPELLBINDER_PERK = "new-horizons:sorceryMagic.spellbinder"
local BASE_DURATION_CAP = 3
local SPELLBINDER_DURATION_CAP = 4
local POWER_PER_EXTRA_ROUND = 80

local function duration(mechanics)
	local base = math.min(BASE_DURATION_CAP,
		1 + math.floor(mechanics:getEffectPower() / POWER_PER_EXTRA_ROUND))
	local hero = mechanics:getHeroCaster()
	if hero ~= nil and hero:hasActivePerk(SPELLBINDER_SKILL, SPELLBINDER_PERK) then
		return math.min(SPELLBINDER_DURATION_CAP, base + 1)
	end
	return base
end

local function opposingBonuses(mechanics, unit)
	local friendly = mechanics:ownerMatches(unit)
	return unit:getBonuses({}):filter(function(bonus)
		if bonus:getSource() ~= ENUM.BonusSource.spellEffect then return false end
		if bonus:getSourceID() == SPELL_KEY then return false end
		local sourceSpell = LIBRARY:getSpellByName(bonus:getSourceID())
		if sourceSpell == nil or sourceSpell:isAdventure() then return false end
		if friendly then return sourceSpell:isNegative() end
		return sourceSpell:isPositive()
	end)
end

function Script:isValidTarget(mechanics, unit)
	return unit ~= nil and unit:isValidTarget(false)
end

function Script:apply(mechanics, server, target)
	local battle = mechanics:getBattle()
	local turns = duration(mechanics)
	for _, destination in ipairs(target) do
		local unit = destination.unit
		if unit == nil or not unit:isAlive() then goto continue end

		local removable = opposingBonuses(mechanics, unit)
		if removable:size() > 0 then
			server:removeUnitBonuses(battle, unit, removable)
		end

		-- Magic resistance prevents hostile follow-up spells in the existing
		-- target pipeline.  The hidden marker carries the full Spell Lock state
		-- for the pending authoritative hook, which must also reject beneficial
		-- follow-up magic and pause existing timed-effect counters.
		server:addUnitBonus(battle, unit, {
			type = "MAGIC_RESISTANCE",
			val = 100,
			duration = ENUM.BonusDuration.nTurns,
			turns = turns,
			sourceType = ENUM.BonusSource.spellEffect,
			sourceID = SPELL_KEY,
			stacking = SPELL_KEY
		}, false)
		server:addUnitBonus(battle, unit, {
			type = "NONE",
			val = turns,
			duration = ENUM.BonusDuration.nTurns,
			turns = turns,
			sourceType = ENUM.BonusSource.spellEffect,
			sourceID = SPELL_KEY,
			hidden = true,
			description = "Spell Lock: preserve the target's remaining magical states"
		}, false)

		::continue::
	end
end

return Script
