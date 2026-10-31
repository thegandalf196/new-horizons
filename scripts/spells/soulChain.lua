local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local SPELL_ID = "new-horizons:soulChain"
local STATUS_TRIGGER = "core:soulChainStatus"
local STATUS_DURATION_ROUNDS = 2
local MAX_TARGETS = 3

local function reject(problem, mechanics)
	if problem then
		problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
	end
	return false
end

local function isValidEnemy(self, mechanics, unit)
	return unit ~= nil and unit:isAlive() and unit:isValidTarget(false)
		and unit:getSide() ~= mechanics:getCasterSide()
		and not unit:isInvincible() and self:isReceptive(mechanics, unit)
end

local function echoBasisPoints(mechanics)
	return mechanics:getSoulChainEchoBasisPoints()
end

function Script:applicableGeneral(mechanics, problem)
	if not mechanics:usesNewHorizonsMagicV3() then
		return reject(problem, mechanics)
	end
	local hasTarget = mechanics:getBattle():getUnitsIf(function(unit)
		return isValidEnemy(self, mechanics, unit)
	end)
	if #hasTarget == 0 then
		return reject(problem, mechanics)
	end
	return true
end

function Script:applicableTarget(mechanics, problem, target)
	if not mechanics:usesNewHorizonsMagicV3() or #target < 1 or #target > MAX_TARGETS then
		return reject(problem, mechanics)
	end
	local selectedIds = {}
	for _, destination in ipairs(target) do
		local unit = destination.unit
		if not isValidEnemy(self, mechanics, unit) then
			return reject(problem, mechanics)
		end
		local unitId = unit:unitID()
		if selectedIds[unitId] then
			return reject(problem, mechanics)
		end
		selectedIds[unitId] = true
	end
	return true
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	if not mechanics:usesNewHorizonsMagicV3() or #aimPoint < 1 or #aimPoint > MAX_TARGETS then
		return {}
	end
	-- Keep the explicit order: the first selection is primary, all others are
	-- secondary victims. Do not let a range/chain transform add or reorder units.
	return aimPoint
end

function Script:apply(mechanics, server, target)
	if not self:applicableTarget(mechanics, nil, target) then
		return
	end
	local primary = target[1].unit
	local battle = mechanics:getBattle()
	local spellKey = mechanics:getSpell():getJsonKey()
	local casterSide = mechanics:getCasterSide()
	local duration = mechanics:adjustEffectDuration(STATUS_DURATION_ROUNDS)
	local echoPercent = echoBasisPoints(mechanics)

	for index = 2, #target do
		local secondary = target[index].unit
		local previous = secondary:getBonuses({
			type = "COMBAT_EVENT_TRIGGER",
			subtype = STATUS_TRIGGER
		}):filter(function(bonus)
			return bonus:getSource() == ENUM.BonusSource.spellEffect
				and bonus:getSourceID() == spellKey
		end)
		if previous:size() > 0 then
			server:removeUnitBonuses(battle, secondary, previous)
		end

		server:addUnitBonus(battle, secondary, {
			type = "COMBAT_EVENT_TRIGGER",
			subtype = STATUS_TRIGGER,
			val = echoPercent,
			duration = ENUM.BonusDuration.nTurns,
			turns = duration,
			sourceType = ENUM.BonusSource.spellEffect,
			sourceID = spellKey,
			stacking = spellKey,
			statusTags = {"DEBUFF"},
			statusIdentity = spellKey,
			addInfo = {
				mdrPenetration = mechanics:getCapturedMdrPenetration(),
				damageMultiplierPercent = mechanics:getHeroLuckDamageMultiplierPercent(),
				primaryUnitId = primary:unitID(),
				casterSide = casterSide
			},
			description = "New Horizons: Soul Chain"
		}, false)
	end
end

return Script
