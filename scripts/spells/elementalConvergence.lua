local Base = require("spells/spellEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local NATURE_MAGIC_SKILL = "new-horizons:natureMagic"
local CONJURER_PERK = "new-horizons:natureMagic.elementalConjurer"

local function hasConjurer(mechanics)
	local hero = mechanics:getHeroCaster()
	return hero ~= nil and hero:hasActivePerk(NATURE_MAGIC_SKILL, CONJURER_PERK)
end

local function healthPool(mechanics)
	local percent = hasConjurer(mechanics) and 130 or 100
	local spellPower = math.max(0, mechanics:getEffectPower())
	-- Preserve fractional rank scaling and the complete-pool perk multiplier
	-- until the single final floor, as for the established Troll summon path.
	local spellPowerHundredths = mechanics:scaleSpellPowerComponentWithCoefficientBasisPoints(
		5 * spellPower * percent, 1, mechanics:getSpellPowerCoefficientBasisPoints())
	return math.floor((250 * percent + spellPowerHundredths) / 100)
end

local function requestedHex(target)
	if target == nil or #target ~= 1 then return nil end
	local destination = target[1]
	if destination == nil or destination.hex == nil or not destination.hex:isValid() then return nil end
	return destination.hex
end

local function legalSelectedHex(mechanics, target)
	local creature = mechanics:getElementalConvergenceCreature()
	local hex = requestedHex(target)
	if creature == nil or hex == nil then return nil end
	local available = mechanics:getBattle():getAvailableHex(creature, mechanics:getCasterSide(), hex)
	if available:isValid() and available == hex then return hex end
	return nil
end

function Script:applicableGeneral(mechanics, problem)
	local creature = mechanics:getElementalConvergenceCreature()
	if creature ~= nil then
		local available = mechanics:getBattle():getAvailableHex(creature, mechanics:getCasterSide())
		if available:isValid() then return true end
	end
	problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
	return false
end

function Script:applicableTarget(mechanics, problem, target)
	if legalSelectedHex(mechanics, target) ~= nil then return true end
	problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
	return false
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	local target = spellTarget
	if (target == nil or #target ~= 1) and aimPoint ~= nil and #aimPoint == 1 then target = aimPoint end
	local hex = legalSelectedHex(mechanics, target)
	if hex == nil then return {} end
	return {{ hex = hex }}
end

function Script:apply(mechanics, server, target)
	local position = legalSelectedHex(mechanics, target)
	if position == nil then return end
	local creature = mechanics:getElementalConvergenceCreature()
	local hpPool = healthPool(mechanics)
	local count = math.ceil(hpPool / mechanics:getSummonedCreatureMaxHealth(creature, true))
	if hpPool <= 0 or count <= 0 then return end
	local battle = mechanics:getBattle()
	local unit = server:addUnit(battle, {
		count = count,
		type = creature,
		side = mechanics:getCasterSide(),
		position = position,
		summoned = true,
		natureSummoned = true
	})
	if unit == nil then return end
	local excess = unit:getAvailableHealth() - hpPool
	if excess > 0 then
		local state = unit:copy()
		state:damage(excess)
		server:changeUnit(battle, state)
	end
	if hasConjurer(mechanics) then
		server:addUnitBonus(battle, unit, {
			type = "STACKS_INITIATIVE_FLAT",
			val = 2,
			duration = ENUM.BonusDuration.nTurns,
			turns = 1,
			sourceType = ENUM.BonusSource.spellEffect,
			sourceID = mechanics:getSpell():getJsonKey()
		}, false)
	end
end

function Script:getHealthChange(mechanics, spellTarget)
	if legalSelectedHex(mechanics, spellTarget) == nil then return { hpDelta = 0, unitsDelta = 0 } end
	local creature = mechanics:getElementalConvergenceCreature()
	local hpPool = healthPool(mechanics)
	return {
		hpDelta = hpPool,
		unitsDelta = math.ceil(hpPool / mechanics:getSummonedCreatureMaxHealth(creature, true)),
		unitType = creature
	}
end

return Script
