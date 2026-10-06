local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local SPELL_ID = "new-horizons:cabirRepair"
local REPAIRABLE_CREATURES = {
	["core:stoneGargoyle"] = true,
	["core:obsidianGargoyle"] = true,
	["core:ironGolem"] = true,
	["core:stoneGolem"] = true,
}

local function isRepairSpell(mechanics)
	return mechanics:getSpell():getJsonKey() == SPELL_ID
end

local function hasSurvivingRepairCaster(mechanics)
	local caster = mechanics:getUnitCaster()
	return caster ~= nil and caster:isAlive() and caster:getCount() > 0
end

function Script:getEffectiveHealLevel(mechanics)
	local caster = mechanics:getUnitCaster()
	if caster and caster:getPhantomInitialIntegrity() > 0 then
		-- As with the shared heal effect, a phantom caster can heal living troops
		-- but cannot restore casualties.
		return ENUM.HealLevel.heal
	end
	return ENUM.HealLevel.resurrect
end

function Script:getRepairPool(mechanics, unit)
	return math.max(0, mechanics:applySpellBonus(mechanics:getEffectValue(), unit))
end

function Script:isValidTarget(mechanics, unit)
	if not isRepairSpell(mechanics) or not hasSurvivingRepairCaster(mechanics)
		or unit == nil or not unit:isAlive() or not unit:isValidTarget(false)
		or unit:getCount() <= 0 or not mechanics:ownerMatches(unit)
		or unit:isGhost() or unit:isSummoned() or unit:isClone()
		or unit:getPhantomInitialIntegrity() > 0 then
		return false
	end

	local creature = unit:getCreature()
	if creature == nil or not REPAIRABLE_CREATURES[creature:getJsonKey()] then
		return false
	end

	local healLevel = self:getEffectiveHealLevel(mechanics)
	if healLevel == ENUM.HealLevel.resurrect then
		-- Match Resurrection's provenance accounting. Casualties recorded as
		-- unusable (for example, Disintegrate) must not create repairable health.
		local creatureHealth = (unit:getCount() - 1) * unit:getMaxHealth() + unit:getFirstHPleft()
		local eligibleInjuries = unit:getTotalHealth() - creatureHealth
			- unit:getUnusableRemains() * unit:getMaxHealth()
		if eligibleInjuries <= 0 then
			return false
		end
	end

	local healedHP, restored = unit:copy():heal(
		self:getRepairPool(mechanics, unit), healLevel, ENUM.HealPower.permanent)
	return healedHP > 0 or restored > 0
end

local function reject(problem, mechanics)
	if problem then
		problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
	end
	return false
end

function Script:applicableGeneral(mechanics, problem)
	if not isRepairSpell(mechanics) or not hasSurvivingRepairCaster(mechanics) then
		return reject(problem, mechanics)
	end

	local targets = mechanics:getBattle():getUnitsIf(function(unit)
		return self:isValidTarget(mechanics, unit) and self:isReceptive(mechanics, unit)
	end)
	if #targets == 0 then
		return reject(problem, mechanics)
	end
	return true
end

function Script:applicableTarget(mechanics, problem, target)
	if not isRepairSpell(mechanics) or not target or #target ~= 1 then
		return reject(problem, mechanics)
	end
	local unit = target[1] and target[1].unit
	if not self:isValidTarget(mechanics, unit) or not self:isReceptive(mechanics, unit) then
		return reject(problem, mechanics)
	end
	return true
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	if not isRepairSpell(mechanics) or #aimPoint ~= 1 or not aimPoint[1].unit then
		return {}
	end
	return { aimPoint[1] }
end

function Script:getHealthChange(mechanics, spellTarget)
	local result = { hpDelta = 0, unitsDelta = 0 }
	if not isRepairSpell(mechanics) or not hasSurvivingRepairCaster(mechanics) then
		return result
	end

	for _, destination in ipairs(spellTarget) do
		local unit = destination.unit
		if unit and self:isValidTarget(mechanics, unit) and self:isReceptive(mechanics, unit) then
			local healedHP, restored = unit:copy():heal(
				self:getRepairPool(mechanics, unit), self:getEffectiveHealLevel(mechanics), ENUM.HealPower.permanent)
			result.hpDelta = result.hpDelta + healedHP
			result.unitsDelta = result.unitsDelta + restored
			result.unitType = unit:getCreature()
		end
	end
	return result
end

function Script:apply(mechanics, server, target)
	if not self:applicableTarget(mechanics, nil, target) then return end

	local unit = target[1].unit
	local healedHP, restored = server:healUnit(mechanics:getBattle(), unit,
		self:getRepairPool(mechanics, unit), self:getEffectiveHealLevel(mechanics), ENUM.HealPower.permanent)
	if restored > 0 then
		local textID = restored == 1 and "core.genrltxt.117" or "core.genrltxt.116"
		server:appendLog(mechanics:getBattle(), {
			append = { textID },
			replaceStrings = { unit:getCreature():getNameTextID(unit:getCount()) },
			replaceNumbers = { restored }
		})
	elseif healedHP > 0 then
		local caster = mechanics:getUnitCaster()
		server:appendLog(mechanics:getBattle(), {
			append = { "core.genrltxt.414" },
			replaceStrings = { caster:getCreature():getNameTextID(1), unit:getCreature():getNameTextID(1) },
			replaceNumbers = { healedHP }
		})
	end
end

return Script
