local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

-- Phantom Army is deliberately not implemented by delegating to core:clone.
-- Clone units have special zero-damage/round-expiry behavior in the engine and
-- therefore cannot represent Phantom Integrity or the physical/magical damage
-- split from the New Horizons rules.
local ILLUSIONIST_SKILL = "new-horizons:sorceryMagic"
local ILLUSIONIST_PERK = "new-horizons:sorceryMagic.illusionist"
local BASE_INTEGRITY_PERCENT = 20
local INTEGRITY_BASIS_POINTS_PER_POWER = 15
local INTEGRITY_PRECISION = 10000
local INTEGRITY_CAP_PERCENT = 40
local ILLUSIONIST_MULTIPLIER = 125
local DURATION_ROUNDS = 2

-- Lua numbers cannot safely multiply the remainder (< 1e10) by the capped
-- integrity numerator (<= 5e9). Multiply by binary long division instead;
-- every intermediate stays below three times the denominator.
local function multiplyDivideFloor(value, multiplier, denominator)
	local quotient = 0
	local remainder = 0
	local bit = 1
	while bit * 2 <= multiplier do bit = bit * 2 end
	while bit >= 1 do
		quotient = quotient * 2
		remainder = remainder * 2
		if math.floor(multiplier / bit) % 2 == 1 then
			remainder = remainder + value
		end
		local carry = math.floor(remainder / denominator)
		quotient = quotient + carry
		remainder = remainder - carry * denominator
		bit = math.floor(bit / 2)
	end
	return quotient
end

local function validSource(mechanics, unit)
	if unit == nil or not unit:isAlive() or not unit:isValidTarget(false) then return false end
	if unit:isClone() or unit:getPhantomInitialIntegrity() > 0 then return false end
	if unit:hasBonuses({type = "SIEGE_WEAPON"}) then return false end
	return mechanics:ownerMatches(unit) and mechanics:isReceptive(unit)
end

local function integrityPool(mechanics, source)
	-- Keep fractional basis points so school rank and Warcasting are both
	-- applied before the single final health floor.
	local basisPointFractions = math.min(INTEGRITY_CAP_PERCENT * 100 * INTEGRITY_PRECISION,
		BASE_INTEGRITY_PERCENT * 100 * INTEGRITY_PRECISION + mechanics:scaleSpellPowerComponentWithCoefficient(
			INTEGRITY_BASIS_POINTS_PER_POWER * mechanics:getEffectPower() * INTEGRITY_PRECISION, 1,
			mechanics:getSchoolRankPowerCoefficientPercent()))
	local multiplier = 100
	local hero = mechanics:getHeroCaster()
	if hero ~= nil and hero:hasActivePerk(ILLUSIONIST_SKILL, ILLUSIONIST_PERK) then
		multiplier = ILLUSIONIST_MULTIPLIER
	end
	-- Keep all intermediates integral and round once, matching the C++ helper.
	-- A floating-point percentage can turn an exact 323 into 322.99999999999994.
	local numerator = basisPointFractions * multiplier
	local denominator = 1000000 * INTEGRITY_PRECISION
	local health = source:getAvailableHealth()
	local pool = math.floor(health / denominator) * numerator
		+ multiplyDivideFloor(health % denominator, numerator, denominator)
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
	-- Creature-aimed casts arrive from the client as a battlefield hex.  The
	-- shared unit-effect resolver finds the occupant before filtering targets.
	local result = Base.transformTarget(self, mechanics, aimPoint, spellTarget)
	if #result > 1 then return { result[1] } end
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
			summoned = true,
			phantomIntegrity = integrity,
			phantomDuration = mechanics:adjustEffectDuration(DURATION_ROUNDS)
		})
		if phantom == nil then goto continue end

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
