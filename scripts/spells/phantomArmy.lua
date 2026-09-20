local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

-- Phantom Army is deliberately not implemented by delegating to core:clone.
-- Clone units have special zero-damage/round-expiry behavior in the engine and
-- therefore cannot represent Phantom Integrity or the physical/magical damage
-- split from the New Horizons rules.
local SPELL_KEY = "new-horizons:phantomArmy"
local ILLUSIONIST_SKILL = "new-horizons:sorceryMagic"
local ILLUSIONIST_PERK = "new-horizons:sorceryMagic.illusionist"
local BASE_INTEGRITY_PERCENT = 20
local INTEGRITY_PER_POWER = 0.15
local INTEGRITY_CAP_PERCENT = 40
local ILLUSIONIST_MULTIPLIER = 125
local DURATION_ROUNDS = 2

local function hasPhantomMarker(unit)
	for _, bonus in ipairs(unit:getBonuses({})) do
		if bonus:getSource() == ENUM.BonusSource.spellEffect
			and bonus:getSourceID() == SPELL_KEY then
			return true
		end
	end
	return false
end

local function validSource(mechanics, unit)
	if unit == nil or not unit:isAlive() or not unit:isValidTarget(false) then return false end
	if unit:isClone() or hasPhantomMarker(unit) then return false end
	if unit:hasBonuses({type = "SIEGE_WEAPON"}) then return false end
	return mechanics:ownerMatches(unit)
end

local function integrityPool(mechanics, source)
	local percent = math.min(INTEGRITY_CAP_PERCENT,
		BASE_INTEGRITY_PERCENT + INTEGRITY_PER_POWER * mechanics:getEffectPower())
	local pool = math.floor(source:getAvailableHealth() * percent / 100)
	local hero = mechanics:getHeroCaster()
	if hero ~= nil and hero:hasActivePerk(ILLUSIONIST_SKILL, ILLUSIONIST_PERK) then
		pool = math.floor(pool * ILLUSIONIST_MULTIPLIER / 100)
	end
	return math.max(1, pool)
end

function Script:isValidTarget(mechanics, unit)
	return validSource(mechanics, unit)
end

function Script:applicableGeneral(mechanics, problem)
	local battle = mechanics:getBattle()
	local creature = nil
	local found = battle:getUnitsIf(function(unit)
		if not validSource(mechanics, unit) then return false end
		creature = unit:getCreature()
		local hex = battle:getAvailableHex(creature, mechanics:getCasterSide(), unit:getPosition())
		return hex:isValid()
	end)
	if #found == 0 then
		problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
		return false
	end
	return true
end

function Script:applicableTarget(mechanics, problem, target)
	for _, destination in ipairs(target) do
		local source = destination.unit
		if validSource(mechanics, source) then
			local hex = mechanics:getBattle():getAvailableHex(
				source:getCreature(), mechanics:getCasterSide(), source:getPosition())
			if hex:isValid() then return true end
		end
	end
	problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
	return false
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	local result = {}
	for _, destination in ipairs(spellTarget) do
		if validSource(mechanics, destination.unit) then
			result[#result + 1] = destination
		end
	end
	return result
end

function Script:apply(mechanics, server, target)
	local battle = mechanics:getBattle()
	for _, destination in ipairs(target) do
		local source = destination.unit
		if not validSource(mechanics, source) then goto continue end

		local creature = source:getCreature()
		local position = battle:getAvailableHex(creature, mechanics:getCasterSide(), source:getPosition())
		if not position:isValid() then goto continue end

		local integrity = integrityPool(mechanics, source)
		local phantom = server:addUnit(battle, {
			count = source:getCount(),
			type = creature,
			side = mechanics:getCasterSide(),
			position = position,
			summoned = true
		})
		if phantom == nil then goto continue end

		-- Keep the aggregate pool exact when the spawned stack's ordinary health is
		-- above the source-derived integrity.  The marker is intentionally a
		-- normal spell-effect bonus: the authoritative C++ damage/expiry hooks can
		-- identify the phantom without abusing UnitState::setCloned.
		local excess = phantom:getAvailableHealth() - integrity
		if excess > 0 then
			local state = phantom:copy()
			state:damage(excess)
			server:changeUnit(battle, state)
		end
		server:addUnitBonus(battle, phantom, {
			type = "NONE",
			val = integrity,
			duration = ENUM.BonusDuration.nTurns,
			turns = DURATION_ROUNDS,
			sourceType = ENUM.BonusSource.spellEffect,
			sourceID = SPELL_KEY,
			hidden = true,
			description = "Phantom Integrity"
		}, false)

		::continue::
	end
end

function Script:getHealthChange(mechanics, spellTarget)
	local source = spellTarget[1] and spellTarget[1].unit
	if source == nil or not validSource(mechanics, source) then
		return { hpDelta = 0, unitsDelta = 0 }
	end
	return {
		hpDelta = integrityPool(mechanics, source),
		unitsDelta = source:getCount(),
		unitType = source:getCreature()
	}
end

return Script
