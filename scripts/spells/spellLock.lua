local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local SPELL_KEY = "new-horizons:spellLock"
local SPELLBINDER_SKILL = "new-horizons:sorceryMagic"
local SPELLBINDER_PERK = "new-horizons:sorceryMagic.spellbinder"
local BASE_DURATION_CAP = 3
local SPELLBINDER_DURATION_CAP = 4
local POWER_PER_EXTRA_ROUND = 80
local PRESERVE_FRIENDLY_TEXT = "new-horizons.combat.spellLock.preserveBeneficial"
local PRESERVE_HOSTILE_TEXT = "new-horizons.combat.spellLock.preserveHostile"

local function duration(mechanics)
	local scaledSpellPower = mechanics:scaleSpellPowerComponentWithCoefficientBasisPoints(
		mechanics:getEffectPower(), POWER_PER_EXTRA_ROUND,
		mechanics:getSpellPowerCoefficientBasisPoints())
	local base = math.min(BASE_DURATION_CAP,
		1 + scaledSpellPower)
	local hero = mechanics:getHeroCaster()
	if hero ~= nil and hero:hasActivePerk(SPELLBINDER_SKILL, SPELLBINDER_PERK) then
		base = math.min(SPELLBINDER_DURATION_CAP, base + 1)
	end
	return mechanics:adjustEffectDuration(base)
end

local function opposingBonuses(mechanics, unit)
	local friendly = mechanics:ownerMatches(unit)
	return unit:getBonuses({}):filter(function(bonus)
		if bonus:getSource() ~= ENUM.BonusSource.spellEffect then return false end
		if bonus:getSourceID() == SPELL_KEY then return false end
		local sourceSpell = LIBRARY:getSpellByName(bonus:getSourceID())
		if sourceSpell == nil or sourceSpell:isAdventure() or not sourceSpell:isMagical() then return false end
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

		local friendly = mechanics:ownerMatches(unit)
		local removable = opposingBonuses(mechanics, unit)
		if removable:size() > 0 then
			server:removeUnitBonuses(battle, unit, removable)
		end

		-- The authoritative spell-target path recognizes this marker and rejects
		-- all follow-up magic. Its sign records which polarity remains protected;
		-- the duration itself is always held in turns and stays positive.
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
			val = friendly and turns or -turns,
			duration = ENUM.BonusDuration.nTurns,
			turns = turns,
			sourceType = ENUM.BonusSource.spellEffect,
			sourceID = SPELL_KEY,
			hidden = true,
			description = friendly
				and "Spell Lock: preserve beneficial magic and freeze its duration"
				or "Spell Lock: preserve hostile magic and freeze its duration"
		}, false)

		if server:describeChanges() then
			server:appendLog(battle, {
				append = {friendly and PRESERVE_FRIENDLY_TEXT or PRESERVE_HOSTILE_TEXT},
				replaceStrings = {unit:getCreature():getNameTextID(unit:getCount())},
				replaceNumbers = {turns}
			})
		end

		::continue::
	end
end

return Script
