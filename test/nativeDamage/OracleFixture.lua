-- Test-only adapter. The production damage scripts are loaded unchanged.
-- Facts stand in for engine observations; this checks numerical evaluation,
-- not the correctness of production observation capture.
ENUM = { BonusSource = { artifact = 1, heroBaseSkill = 2, spellEffect = 3 } }

local function bonus(value, index)
	return {
		getVal = function() return value end,
		getSource = function() return 1 end,
		getSourceID = function() return string.format("source-%08d", index or 1) end,
		getStacking = function() return "" end,
		getSubtype = function() return "oracle-trait" end,
		getParametersAsNumber = function() return 0 end
	}
end

local function list(items)
	return {
		size = function() return #items end,
		getBonus = function(_, i) return items[i] end,
		filter = function(_, predicate)
			local selected = {}
			for _, item in ipairs(items) do
				if predicate(item) then table.insert(selected, item) end
			end
			return list(selected)
		end,
		totalValue = function()
			local total = 0
			for _, item in ipairs(items) do total = total + item:getVal() end
			return total
		end
	}
end

local function grouped(values)
	local result = {}
	for index, value in ipairs(values) do result[index] = bonus(value, index) end
	return list(result)
end

local function unit(data, facts, defending)
	local result = {}
	for method, field in pairs({ getMinDamage="minDamage", getMaxDamage="maxDamage",
		getCount="count", getAttack="attack", getDefense="defense", getFirstHPleft="firstHealth",
		getMaxHealth="maxHealth", getPhantomIntegrity="phantomIntegrity", getBaseAmount="baseAmount",
		getAvailableHealth="availableHealth", isShooter="shooter", isTurret="turret" }) do
		local key = field
		result[method] = function() return data[key] end
	end
	result.getCreature = function() return {getJsonKey = function() return data.creatureKey end} end
	result.hasBonuses = function() return true end -- the one resolved HATES_TRAIT item matches
	result.getBonusesValue = function(_, query)
		local kind = query.type
		if kind == "ENEMY_DEFENCE_REDUCTION" then
			return defending and facts.defenderDefenseReduction or facts.attackerDefenseReduction
		elseif kind == "ENEMY_ATTACK_REDUCTION" then
			return defending and facts.defenderAttackReduction or facts.attackerAttackReduction
		elseif kind == "PERCENTAGE_DAMAGE_BOOST" then return facts.damageBoost
		elseif kind == "BONUS_DAMAGE_PERCENTAGE" then return facts.doubleDamageBonus
		elseif kind == "HATE" then return facts.hatedCreature
		elseif kind == "PRIMARY_SKILL" then
			return query.sourceType == ENUM.BonusSource.artifact and facts.heroArtifactAttack or facts.heroBaseAttack
		elseif kind == "GENERAL_DAMAGE_REDUCTION" then
			if query.sourceType == ENUM.BonusSource.spellEffect then return facts.petrification end
			return facts.physicalProtection.magicShieldReduction
		end
		return data.values[kind] or 0
	end
	result.getBonuses = function(_, query)
		if query.type == "SLAYER" then
			if not facts.hasSlayer then return list({}) end
			local slayer = bonus(facts.slayerValue)
			slayer.getParametersAsNumber = function() return facts.slayerMastery end
			return list({slayer})
		elseif query.type == "HATES_TRAIT" then return list({bonus(facts.hatedTraits)})
		elseif query.type == "PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS" then
			return grouped(facts.physicalProtection.physicalBasisSourceValues)
		elseif query.type == "GENERAL_DAMAGE_REDUCTION" then
			if query.shooting == nil then
				return list({bonus(facts.physicalProtection.legacyGeneralReduction)})
			elseif query.subtype == "damageTypeAll" then
				return grouped(facts.physicalProtection.allDamageSourceValues)
			else return grouped(facts.physicalProtection.subtypeSourceValues) end
		end
		error("Oracle fixture has no bonus-list observation for " .. tostring(query.type))
	end
	return result
end

return function(script, info, facts, prepareOnly)
	info.attacker = unit(facts.attacker, facts, false)
	info.defender = unit(facts.defender, facts, true)
	info.attackerBonuses = facts.attacker.bonuses
	info.defenderBonuses = facts.defender.bonuses
	local battle = {
		hasDistancePenalty = function() return facts.distancePenalty end,
		hasWallPenalty = function() return facts.wallPenalty end,
		isToReverse = function() return facts.toReverse end
	}
	if prepareOnly then
		-- Benchmark setup stays outside the timer; the same production script
		-- still performs every numerical stage and mock observation on each call.
		return function() return script:calculate(battle, info) end
	end
	return script:calculate(battle, info)
end
