local Base = require("spells/spellEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local NATURE_MAGIC_SKILL = "new-horizons:natureMagic"
local BEASTCALLER_PERK = "new-horizons:natureMagic.beastcaller"

local function trollType(self)
	return LIBRARY:getCreatureByName(self.id)
end

local function beastcallerPercent(mechanics)
	local hero = mechanics:getHeroCaster()
	if hero ~= nil and hero:hasActivePerk(NATURE_MAGIC_SKILL, BEASTCALLER_PERK) then
		return 125
	end
	return 100
end

local function healthPool(mechanics)
	local spellPower = math.max(0, mechanics:getEffectPower())
	local modifierPercent = beastcallerPercent(mechanics)
	-- The modifier percent also supplies the hundredths precision: with no perk,
	-- 100 scales the 5/2 coefficient into hundredths; Beastcaller uses 125 so it
	-- scales the complete Spell Power term by 125% before the one final HP floor.
	local spellPowerHundredths = mechanics:scaleSpellPowerComponentWithCoefficientBasisPoints(
		5 * spellPower * modifierPercent,
		2,
		mechanics:getSpellPowerCoefficientBasisPoints())

	return math.floor((100 * modifierPercent + spellPowerHundredths) / 100)
end

local function trollHealth(mechanics, creature)
	return mechanics:getSummonedCreatureMaxHealth(creature, true)
end

local function requestedHex(target)
	if target == nil or #target ~= 1 then return nil end
	local destination = target[1]
	if destination == nil or destination.hex == nil or not destination.hex:isValid() then return nil end
	return destination.hex
end

local function legalSelectedHex(self, mechanics, target)
	local hex = requestedHex(target)
	if hex == nil then return nil end

	local battle = mechanics:getBattle()
	local available = battle:getAvailableHex(trollType(self), mechanics:getCasterSide(), hex)
	if available:isValid() and available == hex then
		return hex
	end
	return nil
end

function Script:applicableGeneral(mechanics, problem)
	local creature = trollType(self)
	local available = mechanics:getBattle():getAvailableHex(creature, mechanics:getCasterSide())
	if available:isValid() then return true end

	problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
	return false
end

function Script:applicableTarget(mechanics, problem, target)
	if legalSelectedHex(self, mechanics, target) ~= nil then return true end

	problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
	return false
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	local target = spellTarget
	if (target == nil or #target ~= 1) and aimPoint ~= nil and #aimPoint == 1 then
		target = aimPoint
	end

	local hex = legalSelectedHex(self, mechanics, target)
	if hex == nil then return {} end
	return {{ hex = hex }}
end

function Script:apply(mechanics, server, target)
	local position = legalSelectedHex(self, mechanics, target)
	if position == nil then return end

	local creature = trollType(self)
	local hpPool = healthPool(mechanics)
	local count = math.ceil(hpPool / trollHealth(mechanics, creature))
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

	-- Use the actual instantiated health so STACK_HEALTH bonuses cannot create
	-- free HP through rounding. The server update keeps this authoritative state
	-- consistent for clients and hypothetical battle evaluation.
	local excess = unit:getAvailableHealth() - hpPool
	if excess > 0 then
		local state = unit:copy()
		state:damage(excess)
		server:changeUnit(battle, state)
	end
end

function Script:getHealthChange(mechanics, spellTarget)
	local hex = legalSelectedHex(self, mechanics, spellTarget)
	if hex == nil then
		return { hpDelta = 0, unitsDelta = 0 }
	end

	local creature = trollType(self)
	local hpPool = healthPool(mechanics)
	return {
		hpDelta = hpPool,
		unitsDelta = math.ceil(hpPool / trollHealth(mechanics, creature)),
		unitType = creature
	}
end

return Script
