-- Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt.
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

-- This first patch delegates to the original base helpers, preserving declared-bonus
-- checks independently of any later patch overriding a helper on the final chain.
local function hasBonusOfType(present, type)
	return Base:hasBonusOfType(present, type)
end

local function getBonusValueOfSubtype(unit, present, type, subtype)
	return Base:getBonusValueOfSubtype(unit, present, type, subtype)
end

local DAMAGE_TYPE_ALL = "damageTypeAll"
local DAMAGE_TYPE_MELEE = "damageTypeMelee"
local DAMAGE_TYPE_RANGED = "damageTypeRanged"

local function hasPhysicalDamageReductionStage(info)
	return info.physicalDamage
		and (info.physicalDamageReductionCapPercent or -1) >= 0
end

-- These existing legacy methods are the same physical protection responsibilities
-- represented by the curated capped stage. They still run once, in factor order.
local legacyPhysicalProtectionFactors = {
	getArmorerFactor = true,
	getNewHorizonsArmorerFactor = true,
	getPaviseFactor = true,
	getBattlecraftDefendFactor = true,
	getMagicShieldFactor = true
}

local function getDefendReductionRemainingFraction(info)
	if not info.physicalDamage or info.shooting then return 1.0 end

	local ignoredPercent = math.max(0,
		math.min(100, info.defensiveStanceDamageReductionIgnorePercent or 0))
	return 1.0 - ignoredPercent / 100
end

--- Values from one source and source ID keep BonusList's value-type rules together.
--- Distinct source IDs remain independent reductions in the physical mitigation stage.
--- This opt-in curated path assumes source-local modifiers; the Lua Bonus API cannot attribute
--- cross-source-ID PERCENT_TO_SOURCE/PERCENT_TO_TARGET_TYPE modifiers to independent groups.
local function getBonusValuesBySourceAndID(bonuses)
	local seen = {}
	local groups = {}
	for index = 1, bonuses:size() do
		local bonus = bonuses:getBonus(index)
		local source = bonus:getSource()
		local sourceID = bonus:getSourceID()
		local sourceIDs = seen[source]
		if not sourceIDs then
			sourceIDs = {}
			seen[source] = sourceIDs
		end
		if not sourceIDs[sourceID] then
			sourceIDs[sourceID] = true
			table.insert(groups, { source, sourceID })
		end
	end

	table.sort(groups, function(left, right)
		if left[1] ~= right[1] then return left[1] < right[1] end
		return left[2] < right[2]
	end)

	local values = {}
	for _, group in ipairs(groups) do
		local source = group[1]
		local sourceID = group[2]
		local value = bonuses:filter(function(bonus)
			return bonus:getSource() == source and bonus:getSourceID() == sourceID
		end):totalValue()
		table.insert(values, value)
	end
	return values
end

local function getPhysicalDamageReductionFactor(info)
	local reductions = {}
	if hasBonusOfType(info.defenderBonuses, "GENERAL_DAMAGE_REDUCTION") then
		local allDamageBonuses = info.defender:getBonuses({
			type = "GENERAL_DAMAGE_REDUCTION",
			subtype = DAMAGE_TYPE_ALL,
			shooting = info.shooting
		}):filter(function(bonus)
			return bonus:getSource() ~= ENUM.BonusSource.spellEffect
		end)
		for _, value in ipairs(getBonusValuesBySourceAndID(allDamageBonuses)) do
			table.insert(reductions, { value, 100 })
		end

		local subtype = info.shooting and DAMAGE_TYPE_RANGED or DAMAGE_TYPE_MELEE
		local subtypeBonuses = info.defender:getBonuses({
			type = "GENERAL_DAMAGE_REDUCTION",
			subtype = subtype,
			shooting = info.shooting
		})
		for _, value in ipairs(getBonusValuesBySourceAndID(subtypeBonuses)) do
			table.insert(reductions, { value, 100 })
		end
	end

	if hasBonusOfType(info.defenderBonuses, "PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS") then
		local physicalBonuses = info.defender:getBonuses({
			type = "PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS",
			shooting = info.shooting
		})
		for _, value in ipairs(getBonusValuesBySourceAndID(physicalBonuses)) do
			table.insert(reductions, { value, 10000 })
		end
	end

	if info.shooting then
		table.insert(reductions, { info.heavenlyGaleDamageReductionBasisPoints or 0, 10000 })
	end

	table.insert(reductions, { info.newHorizonsArmorerReductionPercent or 0, 100 })
	local intrinsicReduction = info.intrinsicPhysicalDamageReductionPercent or 0
	if intrinsicReduction > 0 then
		table.insert(reductions, { intrinsicReduction, 100 })
	end
	table.insert(reductions, { info.runicEnduranceReductionPercent or 0, 100 })
	table.insert(reductions, { info.ironHideReductionPercent or 0, 100 })
	table.insert(reductions, { info.formationFightingReductionPercent or 0, 100 })
	table.insert(reductions, { info.paviseDamageReductionPercent or 0, 100 })
	local defendReductionRemainingFraction = getDefendReductionRemainingFraction(info)
	table.insert(reductions, { (info.battlecraftDefendReductionPercent or 0) * defendReductionRemainingFraction, 100 })
	table.insert(reductions, { (info.bulwarkDamageReductionBasisPoints or 0) * defendReductionRemainingFraction, 10000 })
	local orderReductions = info.heroOrderDamageReductionPercents or {}
	if #orderReductions > 0 then
		for _, value in ipairs(orderReductions) do
			table.insert(reductions, { value, 100 })
		end
	else
		-- Older callers may still provide the original single-reduction field.
		table.insert(reductions, { info.heroOrderDamageReductionPercent or 0, 100 })
	end

	local remainingDamage = 1.0
	for _, reduction in ipairs(reductions) do
		local value = reduction[1]
		local scale = reduction[2]
		local fraction = math.max(0, math.min(scale, value)) / scale
		remainingDamage = remainingDamage * (1 - fraction)
	end

	local capPercent = math.max(0, math.min(100, info.physicalDamageReductionCapPercent))
	local cappedDamageFactor = math.max(remainingDamage, 1 - capPercent / 100)
	local reductionIgnorePercent = math.max(0,
		math.min(100, info.physicalDamageReductionIgnorePercent or 0))
	-- Penetration removes a fraction of the final capped reduction, not a flat
	-- percentage point from one source. Keep the independent-source product and
	-- cap intact before applying this attack-local exception.
	return 1 - (1 - cappedDamageFactor) * (1 - reductionIgnorePercent / 100)
end

--- Armorer and everything else that lessens every kind of blow, other than being petrified.
function Script:physicalProtectionGetArmorerFactor(info)
	if hasPhysicalDamageReductionStage(info) then return 0 end

	local reduction = 0
	if hasBonusOfType(info.defenderBonuses, "GENERAL_DAMAGE_REDUCTION") then
		reduction = info.defender:getBonuses({type = "GENERAL_DAMAGE_REDUCTION", subtype = DAMAGE_TYPE_ALL}):filter(function(bonus)
			return bonus:getSource() ~= ENUM.BonusSource.spellEffect
				and (info.physicalDamage
					or (bonus:getStacking() ~= "new-horizons:ordinary-creature-specialty:core:sorsha:shieldAndSword"
						and bonus:getStacking() ~= "new-horizons:ordinary-creature-specialty:core:marius:demonicSolidarity"))
		end):totalValue()
	end
	-- Breakthrough scales the explicit Defend-derived Bulwark contribution,
	-- not passive general reductions or Orders. Defend's Defense bonus is
	-- handled independently in getDefense().
	return -(reduction + (info.heroOrderDamageReductionPercent or 0)) / 100
		- ((info.bulwarkDamageReductionBasisPoints or 0)
			* getDefendReductionRemainingFraction(info) / 10000)
end

--- New Horizons Armorer is an independent post-Defense reduction source.
function Script:physicalProtectionGetNewHorizonsArmorerFactor(info)
	if hasPhysicalDamageReductionStage(info) then return 0 end
	return -(info.newHorizonsArmorerReductionPercent or 0) / 100
end

--- Pavise is an independent reduction against a Defending stack's ranged physical hit.
function Script:physicalProtectionGetPaviseFactor(info)
	if hasPhysicalDamageReductionStage(info) then return 0 end
	return -(info.paviseDamageReductionPercent or 0) / 100
end

--- Battlecraft's Defend training is independent from Armorer and Orders.
function Script:physicalProtectionGetBattlecraftDefendFactor(info)
	if hasPhysicalDamageReductionStage(info) then return 0 end
	return -(info.battlecraftDefendReductionPercent or 0)
		* getDefendReductionRemainingFraction(info) / 100
end

--- Shield and air shield: each lessens one kind of blow and ignores the other.
function Script:physicalProtectionGetMagicShieldFactor(info)
	if hasPhysicalDamageReductionStage(info) then return 0 end
	local subtype = info.shooting and DAMAGE_TYPE_RANGED or DAMAGE_TYPE_MELEE
	return -getBonusValueOfSubtype(info.defender, info.defenderBonuses, "GENERAL_DAMAGE_REDUCTION", subtype) / 100
end

-- Called at the same pre-factor point as the original capped stage.
function Script:hasPhysicalDamageReductionStage(info)
	return hasPhysicalDamageReductionStage(info)
end

function Script:getPhysicalDamageReductionFactor(info)
	return getPhysicalDamageReductionFactor(info)
end

function Script:isLegacyPhysicalProtectionFactor(method)
	return legacyPhysicalProtectionFactors[method]
end

-- Preserve the original left-to-right uncapped supplements and final relative
-- penetration fold. The factor loop has already collected its original product.
function Script:applyLegacyPhysicalProtection(info, damageFactor,
	usesPhysicalDamageReductionStage, penetratesLegacyProtection, legacyProtectionMultiplier)
	-- New unit passives also coexist with historical uncapped physical rules.
	-- Keep their existing factors and floors; zero/absent adds no arithmetic.
	local intrinsicReduction = info.intrinsicPhysicalDamageReductionPercent or 0
	if info.physicalDamage and not usesPhysicalDamageReductionStage then
		for _, value in ipairs({ intrinsicReduction, info.runicEnduranceReductionPercent or 0, info.ironHideReductionPercent or 0 }) do
			if value > 0 then
				local reduction = math.max(0, math.min(100, value)) / 100
				if penetratesLegacyProtection then
					local existingIgnored = math.max(0, math.min(100, info.legacyPhysicalDamageReductionIgnorePercent or 0)) / 100
					legacyProtectionMultiplier = legacyProtectionMultiplier * (1 - reduction * (1 - existingIgnored))
				else
					local ignored = math.max(0, math.min(100, info.physicalDamageReductionIgnorePercent or 0)) / 100
					damageFactor = damageFactor * (1 - reduction * (1 - ignored))
				end
			end
		end
	end
	if penetratesLegacyProtection then
		-- Correct only a real mark's mixed-policy interaction: one relative ignore
		-- of the existing uncapped aggregate, without introducing a policy cap.
		local ignored = math.max(0, math.min(100, info.coordinatedPhysicalProtectionIgnorePercent or 0)) / 100
		damageFactor = damageFactor * (1 - (1 - legacyProtectionMultiplier) * (1 - ignored))
	end
	return damageFactor
end

return Script
