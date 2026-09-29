local Base = require("spells/heal")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local SPELL_ID = "new-horizons:reanimate"
local SHADOW_MAGIC_SKILL = "new-horizons:shadowMagic"
local REANIMATOR_PERK = "new-horizons:shadowMagic.reanimator"
local BASE_HP = 220
local HP_PER_SPELL_POWER = 5
local SCHOOL_SPELLCRAFT_BASIS_POINTS = 10000

local function reject(problem, mechanics)
	if problem then
		problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
	end
	return false
end

local function usesCanonicalRules(mechanics)
	return mechanics:usesNewHorizonsMagicV3()
		and mechanics:getSpell():getJsonKey() == SPELL_ID
end

local function isBlockedCorpse(mechanics, unit)
	if not unit:isDead() then return false end
	local hexes = unit:getHexes()
	local battle = mechanics:getBattle()
	for index = 1, hexes:size() do
		local hex = hexes:at(index)
		local blockers = battle:getUnitsIf(function(other)
			return other ~= unit and other:isValidTarget(false) and other:coversPos(hex)
		end)
		if #blockers > 0 then return true end
	end
	return false
end

function Script:getHealLevel()
	return ENUM.HealLevel.resurrect
end

function Script:getHealPower()
	return ENUM.HealPower.oneBattle
end

function Script:isValidTarget(mechanics, unit)
	if not usesCanonicalRules(mechanics) or mechanics:getHeroCaster() == nil
		or unit == nil or not unit:isValidTarget(true) or unit:isGhost()
		or unit:isClone() or unit:getPhantomInitialIntegrity() > 0
		or not mechanics:ownerMatches(unit) then
		return false
	end

	-- Subtract the provenance ledger for living as well as dead stacks. This
	-- prevents unusable casualties from making an otherwise full surviving stack
	-- look healable, while wounds on surviving creatures remain healable.
	local creatureHealth = 0
	if unit:getCount() > 0 then
		-- getAvailableHealth includes temporary shields, but CHealth::heal caps
		-- Resurrection against creature HP. Reconstruct that same pool from the
		-- first creature and remaining full survivors.
		creatureHealth = (unit:getCount() - 1) * unit:getMaxHealth() + unit:getFirstHPleft()
	end
	local injuries = unit:getTotalHealth() - creatureHealth
		- unit:getUnusableRemains() * unit:getMaxHealth()
	return injuries > 0 and not isBlockedCorpse(mechanics, unit)
end

local function survivorWounds(unit)
	if not unit:isAlive() then return 0 end
	return math.max(0, unit:getMaxHealth() - unit:getFirstHPleft())
end

local function healingPool(mechanics, unit)
	local hero = mechanics:getHeroCaster()
	local rawSpellPower = math.max(0, hero:getPrimarySkill(ENUM.PrimarySkill.spellpower))
	local coefficientBasisPoints = mechanics:getSpellPowerCoefficientBasisPoints()
	-- These integer inputs keep the product below 2^53; the coefficient's
	-- 1/10000 granularity also stays wider than a floating-point ULP here.
	local scaledPower = math.floor(HP_PER_SPELL_POWER * rawSpellPower
		* coefficientBasisPoints / SCHOOL_SPELLCRAFT_BASIS_POINTS)
	local pool = BASE_HP + scaledPower
	if hero:hasActivePerk(SHADOW_MAGIC_SKILL, REANIMATOR_PERK) then
		pool = pool + math.floor(math.max(0, pool - survivorWounds(unit)) / 4)
	end
	return pool
end

function Script:applicableGeneral(mechanics, problem)
	if not usesCanonicalRules(mechanics) or mechanics:getHeroCaster() == nil then
		return reject(problem, mechanics)
	end
	local targets = mechanics:getBattle():getUnitsIf(function(unit)
		return self:isValidTarget(mechanics, unit) and self:isReceptive(mechanics, unit)
	end)
	if #targets == 0 then return reject(problem, mechanics) end
	return true
end

function Script:applicableTarget(mechanics, problem, target)
	if not usesCanonicalRules(mechanics) or mechanics:getHeroCaster() == nil
		or #target ~= 1 or not self:isValidTarget(mechanics, target[1].unit)
		or not self:isReceptive(mechanics, target[1].unit) then
		return reject(problem, mechanics)
	end
	return true
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	if not usesCanonicalRules(mechanics) or #aimPoint ~= 1 or not aimPoint[1].unit then
		return {}
	end
	return { aimPoint[1] }
end

function Script:getHealthChange(mechanics, spellTarget)
	local result = { hpDelta = 0, unitsDelta = 0 }
	if not usesCanonicalRules(mechanics) then return result end
	for _, destination in ipairs(spellTarget) do
		local unit = destination.unit
		if unit and self:isValidTarget(mechanics, unit) and self:isReceptive(mechanics, unit) then
			local copy = unit:copy()
			local healedHP, resurrected = copy:heal(healingPool(mechanics, unit),
				ENUM.HealLevel.resurrect, ENUM.HealPower.oneBattle)
			result.hpDelta = result.hpDelta + healedHP
			result.unitsDelta = result.unitsDelta + resurrected
			result.unitType = unit:getCreature()
		end
	end
	return result
end

function Script:apply(mechanics, server, target)
	if not self:applicableTarget(mechanics, nil, target) then return end

	local unit = target[1].unit
	local battle = mechanics:getBattle()
	local healedHP, resurrected = server:healUnit(battle, unit, healingPool(mechanics, unit),
		ENUM.HealLevel.resurrect, ENUM.HealPower.oneBattle)
	if resurrected > 0 then
		server:appendLog(battle, {
			append = {"new-horizons.combat.reanimate.restored"},
			replaceStrings = {unit:getCreature():getNameTextID(unit:getCount())},
			replaceNumbers = {healedHP, resurrected}
		})
	elseif healedHP > 0 then
		server:appendLog(battle, {
			append = {"new-horizons.combat.reanimate.healed"},
			replaceStrings = {unit:getCreature():getNameTextID(unit:getCount())},
			replaceNumbers = {healedHP}
		})
	end
end

return Script
