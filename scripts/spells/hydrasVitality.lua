local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local SPELL_ID = "new-horizons:hydrasVitality"
local BASE_PERCENT_MILLIONTHS = 25000000
local MAX_PERCENT_MILLIONTHS = 50000000
local PERCENT_MILLIONTHS_PER_WHOLE = 100000000
local BASE_DURATION_ROUNDS = 3

local function reject(problem, mechanics)
	if problem then
		problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
	end
	return false
end

local function usesCanonicalRules(mechanics)
	return mechanics:getSpell():getJsonKey() == SPELL_ID
		and mechanics:usesNewHorizonsMagicV3()
end

local function percentMillionths(mechanics)
	-- The shared spell mechanics returns percent-millionths (1e-4 of a basis
	-- point), already including effective rank and school modifiers.
	return math.max(BASE_PERCENT_MILLIONTHS,
		math.min(MAX_PERCENT_MILLIONTHS, mechanics:getEffectValue()))
end

local function enhancedMaximum(referenceMaximum, percent)
	-- Compute floor(reference * (1 + percent/100M)) without a large
	-- intermediate floating-point product. The decomposition is exact and has
	-- only one mathematical floor: the final whole-HP maximum.
	local scaledPart = math.floor(referenceMaximum / PERCENT_MILLIONTHS_PER_WHOLE)
	local remainder = referenceMaximum % PERCENT_MILLIONTHS_PER_WHOLE
	local increase = scaledPart * percent
		+ math.floor(remainder * percent / PERCENT_MILLIONTHS_PER_WHOLE)
	return referenceMaximum + increase
end

local function targetMaximum(mechanics, unit)
	if unit == nil then return nil end
	local state = unit:copy()
	local referenceMaximum = state:getCapacityHealthReferenceMax()
	local maximum = enhancedMaximum(referenceMaximum, percentMillionths(mechanics))
	if maximum > 2147483647 then
		-- Bonus values are signed 32-bit health capacities. Reject before costs
		-- are paid rather than allowing the application path to wrap.
		return nil
	end
	return maximum
end

local function isEligibleTarget(self, mechanics, unit)
	return unit ~= nil and unit:isAlive() and unit:isValidTarget(false)
		and unit:isLiving() and not unit:isClone()
		and unit:getPhantomInitialIntegrity() <= 0
		and mechanics:ownerIsSameAs(unit)
		and targetMaximum(mechanics, unit) ~= nil
		and self:isReceptive(mechanics, unit)
end

local function targetFromDestinations(target)
	if target == nil or #target ~= 1 then return nil end
	return target[1].unit
end

function Script:isValidTarget(mechanics, unit)
	return usesCanonicalRules(mechanics) and isEligibleTarget(self, mechanics, unit)
end

function Script:applicableGeneral(mechanics, problem)
	if not usesCanonicalRules(mechanics) then
		return reject(problem, mechanics)
	end
	local targets = mechanics:getBattle():getUnitsIf(function(unit)
		return isEligibleTarget(self, mechanics, unit)
	end)
	if #targets == 0 then
		return reject(problem, mechanics)
	end
	return true
end

function Script:applicableTarget(mechanics, problem, target)
	if not usesCanonicalRules(mechanics) or #target ~= 1
		or not isEligibleTarget(self, mechanics, targetFromDestinations(target)) then
		return reject(problem, mechanics)
	end
	return true
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	if not usesCanonicalRules(mechanics) then return {} end
	-- Resolve both unit selections and raw hex-only human selections using the
	-- common range transformer. It also deduplicates a wide creature selected
	-- through both head and tail hexes. Hydra's Vitality remains strictly
	-- single-target after that resolution.
	local resolved = Base.transformByRange(self, mechanics, aimPoint, spellTarget)
	if #resolved ~= 1 or not isEligibleTarget(self, mechanics, resolved[1].unit) then
		return {}
	end
	return resolved
end

function Script:getHealthChange(mechanics, spellTarget)
	-- The increased maximum is capacity only. Cast-time current HP and casualties
	-- are unchanged; healing occurs later at genuine creature activations.
	return { hpDelta = 0, unitsDelta = 0 }
end

function Script:apply(mechanics, server, target)
	if not self:applicableTarget(mechanics, nil, target) then return end
	local unit = target[1].unit
	local battle = mechanics:getBattle()
	local spellKey = mechanics:getSpell():getJsonKey()
	local targetMax = targetMaximum(mechanics, unit)
	if targetMax == nil then return end
	local state = unit:copy()
	local referenceMaximum = state:getCapacityHealthReferenceMax()
	state:preserveCreatureHealthOnCapacityIncrease()
	-- targetMaximum uses the pre-cast baseline on a recast and preserves the
	-- shared effect-value precision through one final maximum-HP floor.
	if referenceMaximum <= 0 then return end

	-- This UPDATE is deliberately authoritative and precedes the capacity
	-- bonus. It snapshots every rear survivor at the old maximum so the later
	-- bonus cannot turn empty HP capacity into current HP.
	server:changeUnit(battle, state)

	local duration = mechanics:adjustEffectDuration(BASE_DURATION_ROUNDS)
	server:addUnitBonus(battle, unit, {
		type = "STACK_HEALTH",
		val = targetMax,
		valueType = "INDEPENDENT_MAX",
		duration = ENUM.BonusDuration.nTurns,
		turns = duration,
		sourceType = ENUM.BonusSource.spellEffect,
		sourceID = spellKey,
		stacking = spellKey,
		description = "New Horizons: Hydra's Vitality"
	}, false)
	server:addUnitBonus(battle, unit, {
		type = "HP_REGENERATION",
		val = 0,
		duration = ENUM.BonusDuration.nTurns,
		turns = duration,
		sourceType = ENUM.BonusSource.spellEffect,
		sourceID = spellKey,
		stacking = spellKey,
		description = "New Horizons: Hydra's Vitality"
	}, false)
end

return Script
