local Base = require("spells/unitEffect")
local Damage = require("spells/damage")
local BattleLog = require("battleLog")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local SHADOW_MAGIC_SKILL = "new-horizons:shadowMagic"
local BLOOD_DRINKER_PERK = "new-horizons:shadowMagic.bloodDrinker"
local BASE_HEAL_PERCENT = 60
local BLOOD_DRINKER_HEAL_PERCENT = 75

local function addNoTargetProblem(mechanics, problem)
	if problem then
		problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
	end
	return false
end

local function livingTarget(unit)
	return unit ~= nil and unit:isAlive() and unit:isValidTarget(false)
end

local function isEnemyTarget(self, mechanics, unit)
	return livingTarget(unit)
		and mechanics:ownerMatches(unit)
		and self:isReceptive(mechanics, unit)
end

local function isFriendlyTarget(mechanics, unit)
	return livingTarget(unit) and mechanics:ownerIsSameAs(unit)
end

local function healPercent(mechanics)
	local hero = mechanics:getHeroCaster()
	if hero and hero:hasActivePerk(SHADOW_MAGIC_SKILL, BLOOD_DRINKER_PERK) then
		return BLOOD_DRINKER_HEAL_PERCENT
	end
	return BASE_HEAL_PERCENT
end

local function predictedDamage(mechanics, unit)
	local copy = unit:copy()
	local before = copy:getAvailableHealth()
	local countBefore = copy:getCount()
	copy:damage(mechanics:adjustEffectValue(unit))
	return before - copy:getAvailableHealth(), countBefore - copy:getCount()
end

function Script:adjustTargetTypes(mechanics, types)
	if #types == 0 then return types end
	if types[1] ~= ENUM.AimType.creature then return {} end
	if #types == 1 then return { ENUM.AimType.creature, ENUM.AimType.creature } end
	if #types ~= 2 or types[2] ~= ENUM.AimType.creature then return {} end
	return types
end

function Script:isReceptive(mechanics, unit)
	-- Keep spell resistance and the saved New Horizons magical-reduction
	-- compatibility rule identical to ordinary direct damage spells.
	return Damage.isReceptive(self, mechanics, unit)
end

function Script:applicableGeneral(mechanics, problem)
	if not mechanics:usesNewHorizonsMagicV3() then
		return addNoTargetProblem(mechanics, problem)
	end

	local hasEnemy = false
	local hasFriend = false
	mechanics:getBattle():getUnitsIf(function(unit)
		if not hasEnemy and isEnemyTarget(self, mechanics, unit) then hasEnemy = true end
		if not hasFriend and isFriendlyTarget(mechanics, unit) then hasFriend = true end
		return hasEnemy and hasFriend
	end)

	if not hasEnemy or not hasFriend then
		return addNoTargetProblem(mechanics, problem)
	end
	return true
end

function Script:applicableTarget(mechanics, problem, target)
	if not mechanics:usesNewHorizonsMagicV3() or #target == 0 or #target > 2 then
		return addNoTargetProblem(mechanics, problem)
	end

	local enemy = target[1].unit
	if not isEnemyTarget(self, mechanics, enemy) then
		return addNoTargetProblem(mechanics, problem)
	end

	-- The enemy prefix is deliberately valid so the target resolver can request
	-- the second, friendly creature selection in order.
	if #target == 1 then return true end
	if not isFriendlyTarget(mechanics, target[2].unit) then
		return addNoTargetProblem(mechanics, problem)
	end
	return true
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	if not mechanics:usesNewHorizonsMagicV3() then return {} end
	if #aimPoint > 0 and aimPoint[1].unit then
		spellTarget = { aimPoint[1] }
	end
	local filtered = Base.transformTarget(self, mechanics, aimPoint, spellTarget)
	if #filtered == 0 then return {} end

	local result = { filtered[1] }
	if #aimPoint >= 2 then
		-- Preserve the explicit second identity so an invalid friendly selection
		-- cannot silently collapse into a valid one-target enemy prefix.
		result[2] = aimPoint[2]
	end
	return result
end

function Script:getHealthChange(mechanics, spellTarget)
	local result = { hpDelta = 0, unitsDelta = 0 }
	if not mechanics:usesNewHorizonsMagicV3() or #spellTarget == 0 then return result end

	local enemy = spellTarget[1].unit
	if not isEnemyTarget(self, mechanics, enemy) then return result end
	local damage, killed = predictedDamage(mechanics, enemy)
	result.hpDelta = -damage
	result.unitsDelta = -killed
	-- The generic hover value is attached to the selected enemy stack. Do not
	-- combine a separate friendly heal into that number; cast evaluation records
	-- both authoritative unit-state changes independently.
	return result
end

function Script:apply(mechanics, server, target)
	if not mechanics:usesNewHorizonsMagicV3() or #target ~= 2 then return end
	if not self:applicableTarget(mechanics, nil, target) then return end

	local enemy = target[1].unit
	local friendly = target[2].unit
	local battle = mechanics:getBattle()
	local enemyID = enemy:unitID()
	local damage, killed = server:damageUnit(
		battle, enemy, mechanics:adjustEffectValue(enemy), false, mechanics:getUnitCaster(), true)
	local current = battle:getUnitByID(enemyID)
	if current then server:clearFrozenAfterDirectMagicDamage(battle, current) end
	local healing = math.floor(damage * healPercent(mechanics) / 100)
	local healedHP = 0
	if healing > 0 then
		healedHP = server:healUnit(battle, friendly, healing, ENUM.HealLevel.heal, ENUM.HealPower.permanent)
	end

	if damage > 0 then
		BattleLog.spellDamage(server, battle, mechanics:getSpell(), enemy, damage, killed)
	end
	if healedHP > 0 then
		server:appendLog(battle, {
			append = { "new-horizons.combat.lifeDrain.healed" },
			replaceStrings = { friendly:getCreature():getNameTextID(friendly:getCount()) },
			replaceNumbers = { healedHP }
		})
	end
end

return Script
