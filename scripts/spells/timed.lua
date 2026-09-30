local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local HOLY_ARMOR_SPELL = "new-horizons:holyArmor"
local HEAVENLY_GALE_SPELL = "new-horizons:heavenlyGale"
local GUARDIAN_SPIRIT_SPELL = "new-horizons:guardianSpirit"
local DIVINE_RETRIBUTION_SPELL = "new-horizons:divineRetribution"
local CRUSADE_SPELL = "new-horizons:crusade"
local ENTANGLE_SPELL = "new-horizons:entangle"
local VENGEFUL_VINES_SPELL = "new-horizons:vengefulVines"
local MISFORTUNE_SPELL = "core:misfortune"
local LIGHT_MAGIC_SKILL = "new-horizons:lightMagic"
local HEALER_PERK = "new-horizons:lightMagic.healer"
local GUARDIAN_PERK = "new-horizons:lightMagic.guardian"
local AEGIS_PERK = "new-horizons:lightMagic.aegis"
local RETRIBUTIONIST_PERK = "new-horizons:lightMagic.retributionist"
local CRUSADER_PERK = "new-horizons:lightMagic.crusader"
local NATURE_MAGIC_SKILL = "new-horizons:natureMagic"
local ROOTCALLER_PERK = "new-horizons:natureMagic.rootcaller"
local CHAOS_MAGIC_SKILL = "new-horizons:chaosMagic"
local MISFORTUNE_WEAVER_PERK = "new-horizons:chaosMagic.misfortuneWeaver"
local HOLY_ARMOR_SPELL_POWER_DIVISOR = 5
local HOLY_ARMOR_MAX_REDUCTION_PERCENT = 60
local HEAVENLY_GALE_BASE_REDUCTION_BASIS_POINTS = 5000
local HEAVENLY_GALE_SPELL_POWER_COEFFICIENT_BASIS_POINTS = 15
local HEAVENLY_GALE_MAX_REDUCTION_BASIS_POINTS = 8000
local DIVINE_RETRIBUTION_BASE_DAMAGE = 25
local DIVINE_RETRIBUTION_POWER_NUMERATOR = 125
local DIVINE_RETRIBUTION_POWER_DIVISOR = 100
local CRUSADE_BASE_ATTRIBUTE_BONUS = 3
local CRUSADE_ATTRIBUTE_POWER_DIVISOR = 75
local CRUSADE_BASE_INITIATIVE_BONUS = 1
local CRUSADE_INITIATIVE_POWER_DIVISOR = 100
local CRUSADE_BASE_REDUCTION_BASIS_POINTS = 1200
local CRUSADE_REDUCTION_POWER_BASIS_POINTS_NUMERATOR = 13
local CRUSADE_REDUCTION_POWER_DIVISOR = 2
local CRUSADE_MAX_REDUCTION_BASIS_POINTS = 2500
local CRUSADE_BASE_DURATION = 3
local ENTANGLE_BASE_DURATION = 1
local ENTANGLE_SPELL_POWER_DIVISOR = 100
local ENTANGLE_MAX_BASE_DURATION = 2
local ENTANGLE_MAX_ROOTCALLER_DURATION = 3
local VENGEFUL_VINES_BASE_DURATION = 2
local MISFORTUNE_BASE_DURATION = 2
local MISFORTUNE_MAX_DURATION = 4
local MISFORTUNE_SPELL_POWER_DURATION_DIVISOR = 80
local MISFORTUNE_BASE_CHANCE_MULTIPLIER_BASIS_POINTS = 7500
local MISFORTUNE_SPELL_POWER_REDUCTION_BASIS_POINTS = 25
local MISFORTUNE_WEAVER_REDUCTION_BASIS_POINTS = 1000
local MISFORTUNE_MIN_CHANCE_MULTIPLIER_BASIS_POINTS = 2500
local SHIELD_OF_CHAOS_SPELL = "new-horizons:shieldOfChaos"
local PARADOX_SHIELD_PERK = "new-horizons:chaosMagic.paradoxShield"
local SHIELD_OF_CHAOS_BASE_REDUCTION_BASIS_POINTS = 5000
local SHIELD_OF_CHAOS_SPELL_POWER_REDUCTION_BASIS_POINTS = 15
local SHIELD_OF_CHAOS_MAX_REDUCTION_BASIS_POINTS = 8000
local PARADOX_SHIELD_BONUS_BASIS_POINTS = 1000
local SHIELD_OF_CHAOS_BASE_DURATION = 2
local SLOW_SPELL = "core:slow"
local SLOW_BASE_REDUCTION_PERCENT = 20
local SLOW_SPELL_POWER_DIVISOR = 5
local SLOW_MAX_REDUCTION_PERCENT = 50

function Script:deepCopyBonus(b)
	local copy = {}
	for k, v in pairs(b) do
		copy[k] = v
	end
	return copy
end

function Script:convertBonuses(mechanics)
	local spellKey = mechanics:getSpell():getJsonKey()
	local duration = nil
	local crusadeDuration = nil
	local entangleDuration = nil
	local vengefulVinesDuration = nil
	local misfortuneDuration = nil
	local shieldOfChaosDuration = nil
	local misfortuneChanceMultiplierBasisPoints = nil
	if spellKey == SHIELD_OF_CHAOS_SPELL and mechanics:usesNewHorizonsMagicV3() then
		shieldOfChaosDuration = mechanics:adjustEffectDuration(SHIELD_OF_CHAOS_BASE_DURATION)
	elseif spellKey == CRUSADE_SPELL and mechanics:usesNewHorizonsMagicV3() then
		-- Crusade authors a fixed three-round buff, independent of Spell Power. Apply
		-- cast-specific duration mechanics exactly once to that literal base, then
		-- add Crusader's separate round below.
		crusadeDuration = mechanics:adjustEffectDuration(CRUSADE_BASE_DURATION)
		local hero = mechanics:getHeroCaster()
		if hero and hero:hasActivePerk(LIGHT_MAGIC_SKILL, CRUSADER_PERK) then
			crusadeDuration = crusadeDuration + 1
		end
	elseif spellKey == ENTANGLE_SPELL and mechanics:usesNewHorizonsMagicV3() then
		-- School rank strengthens only the Spell Power term. Rootcaller extends
		-- the capped base before Echoed Duration adds its separate cast round.
		local spellPowerTerm = mechanics:scaleSpellPowerComponentWithCoefficientBasisPoints(
			math.max(0, mechanics:getEffectPower()), ENTANGLE_SPELL_POWER_DIVISOR,
			mechanics:getSpellPowerCoefficientBasisPoints())
		local rootDuration = math.min(ENTANGLE_MAX_BASE_DURATION,
			ENTANGLE_BASE_DURATION + spellPowerTerm)
		local hero = mechanics:getHeroCaster()
		if hero and hero:hasActivePerk(NATURE_MAGIC_SKILL, ROOTCALLER_PERK) then
			rootDuration = math.min(ENTANGLE_MAX_ROOTCALLER_DURATION, rootDuration + 1)
		end
		entangleDuration = mechanics:adjustEffectDuration(rootDuration)
	elseif spellKey == VENGEFUL_VINES_SPELL and mechanics:usesNewHorizonsMagicV3() then
		-- Vengeful Vines has a fixed two-round Speed penalty. Apply only the
		-- common cast-specific Echoed Duration extension to that literal base.
		vengefulVinesDuration = mechanics:adjustEffectDuration(VENGEFUL_VINES_BASE_DURATION)
	elseif spellKey == MISFORTUNE_SPELL and mechanics:usesNewHorizonsMagicV3() then
		-- Misfortune's fixed duration and chance floor stay outside the saved
		-- School/Spellcraft coefficient. Only the Spell Power terms are scaled.
		local spellPower = math.max(0, mechanics:getEffectPower())
		local coefficient = mechanics:getSpellPowerCoefficientBasisPoints()
		local durationPowerTerm = mechanics:scaleSpellPowerComponentWithCoefficientBasisPoints(
			spellPower, MISFORTUNE_SPELL_POWER_DURATION_DIVISOR, coefficient)
		local ordinaryDuration = math.min(MISFORTUNE_MAX_DURATION,
			MISFORTUNE_BASE_DURATION + durationPowerTerm)
		misfortuneDuration = mechanics:adjustEffectDuration(ordinaryDuration)

		local powerChanceReduction = mechanics:scaleSpellPowerComponentWithCoefficientBasisPoints(
			MISFORTUNE_SPELL_POWER_REDUCTION_BASIS_POINTS * spellPower, 1, coefficient)
		local chanceMultiplier = MISFORTUNE_BASE_CHANCE_MULTIPLIER_BASIS_POINTS - powerChanceReduction
		local hero = mechanics:getHeroCaster()
		if hero and hero:hasActivePerk(CHAOS_MAGIC_SKILL, MISFORTUNE_WEAVER_PERK) then
			chanceMultiplier = chanceMultiplier - MISFORTUNE_WEAVER_REDUCTION_BASIS_POINTS
		end
		misfortuneChanceMultiplierBasisPoints = math.max(
			MISFORTUNE_MIN_CHANCE_MULTIPLIER_BASIS_POINTS, chanceMultiplier)
	else
		duration = mechanics:getEffectDuration()
	end
	local newHorizonsSlowReduction = nil
	if spellKey == SLOW_SPELL and mechanics:usesNewHorizonsMagicV3() then
		local spellPowerTerm = mechanics:scaleSpellPowerComponentWithCoefficientBasisPoints(
			mechanics:getEffectPower(), SLOW_SPELL_POWER_DIVISOR,
			mechanics:getSpellPowerCoefficientBasisPoints())
		newHorizonsSlowReduction = -math.min(SLOW_MAX_REDUCTION_PERCENT,
			SLOW_BASE_REDUCTION_PERCENT + spellPowerTerm)
	end
	local converted = {}

	for name, b in pairs(self.bonus or {}) do
		local nb = self:deepCopyBonus(b)
		if misfortuneDuration ~= nil and name == "luck" then
			-- New Horizons Misfortune suppresses only positive Luck. The timed
			-- zero cap leaves negative Luck and the stack's ordinary bonuses intact.
			nb.type = "MAXIMUM_LUCK"
			nb.val = 0
		end
		if name == "stacksSpeed" and newHorizonsSlowReduction ~= nil then
			-- New Horizons v3 keeps Slow's fixed 20% base outside the school-rank
			-- coefficient and caps the ordinary magnitude before specialties apply.
			nb.val = newHorizonsSlowReduction
		end
		if mechanics:usesNewHorizonsMagicV3() then
			if (spellKey == "core:bless" and name == "alwaysMaximumDamage")
				or (spellKey == "core:curse" and name == "alwaysMinimumDamage") then
				-- New Horizons v3 collapses Bless/Curse onto the natural damage
				-- endpoint; vanilla's rank-based +/-1 does not carry over.
				nb.val = 0
			end
		end
		if spellKey == SLOW_SPELL and mechanics:usesNewHorizonsMagic() then
			-- New Horizons separates turn-order Initiative from movement Speed.
			-- STACKS_INITIATIVE stores a direct percentage delta consumed only by
			-- CUnitState::getInitiative; movement range remains unchanged.
			nb.type = "STACKS_INITIATIVE"
			nb.valueType = "ADDITIVE_VALUE"
		end
		if misfortuneDuration ~= nil then
			nb.turns = misfortuneDuration
		elseif entangleDuration ~= nil then
			nb.turns = entangleDuration
		elseif vengefulVinesDuration ~= nil then
			nb.turns = vengefulVinesDuration
		elseif crusadeDuration ~= nil then
			nb.turns = crusadeDuration
		elseif shieldOfChaosDuration ~= nil then
			nb.turns = shieldOfChaosDuration
		elseif not nb.turns or nb.turns == 0 then
			nb.turns = duration
		end

		nb.sourceType = "SPELL_EFFECT"
		nb.sourceID = spellKey

		converted[name] = nb
	end

	if misfortuneChanceMultiplierBasisPoints ~= nil then
		converted.favorableCreatureChanceMultiplier = {
			type = "FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS",
			duration = ENUM.BonusDuration.nTurns,
			val = misfortuneChanceMultiplierBasisPoints,
			valueType = "INDEPENDENT_MIN",
			turns = misfortuneDuration,
			sourceType = "SPELL_EFFECT",
			sourceID = spellKey
		}
	end

	return converted
end

function Script:applyTemporalFieldScale(mechanics, buffer, spellKey)
	if not mechanics:isMassSlow() or spellKey ~= SLOW_SPELL then return end
	for _, nb in pairs(buffer) do
		-- Apply after every target-specific hero specialty so Temporal Field is
		-- exactly 60% of the ordinary Slow magnitude that target would receive.
		-- Slow values are negative; ceil preserves the sign while rounding the
		-- reduced magnitude toward zero.
		nb.val = math.ceil((nb.val or 0) * 60 / 100)
	end
end

--- Shifts every buffered bonus value by a per-target-tier amount (weakness/slayer-style).
function Script:applyPeculiarEnchant(mechanics, hero, buffer, tier, spellKey)
	local peculiar = hero:getBonuses({type = "SPECIAL_PECULIAR_ENCHANT", subtype = spellKey})
	if peculiar:size() == 0 then return end

	local bonus = peculiar:getBonus(1)
	local levels = bonus:getParametersAsVector()
	local power = 0
	if #levels > 0 then
		-- explicit per-tier values from addInfo array (sign included), clamping (tier - 1) into bounds
		local idx = math.max(1, math.min(#levels, tier))
		power = levels[idx]
	else
		if tier <= 2 then power = 3
		elseif tier <= 4 then power = 2
		elseif tier <= 6 then power = 1
		end
		if mechanics:isNegative() then power = -power end
	end
	if power ~= 0 then
		for _, nb in pairs(buffer) do
			nb.val = (nb.val or 0) + power
		end
	end
end

--- Adds a flat amount to every buffered bonus value (Aenain-style).
function Script:applyAddValueEnchant(mechanics, hero, buffer, tier, spellKey)
	local addVal = hero:getBonuses({type = "SPECIAL_ADD_VALUE_ENCHANT", subtype = spellKey})
	if addVal:size() == 0 then return end

	local addAmount = addVal:getBonus(1):getParametersAsNumber()
	for _, nb in pairs(buffer) do
		nb.val = (nb.val or 0) + addAmount
	end
end

--- Overwrites every buffered bonus value with a fixed amount (Daremyth-style).
function Script:applyFixedValueEnchant(mechanics, hero, buffer, tier, spellKey)
	local fixedVal = hero:getBonuses({type = "SPECIAL_FIXED_VALUE_ENCHANT", subtype = spellKey})
	if fixedVal:size() == 0 then return end

	local fixedAmount = fixedVal:getBonus(1):getParametersAsNumber()
	for _, nb in pairs(buffer) do
		nb.val = fixedAmount
	end
end

--- Scales every buffered bonus value by a per-target-tier percentage (Solmyr-style
--- SPECIAL_SPELL_SCALING, matching CGHeroInstance::getSpellBonus but for buff/debuff vals).
function Script:applySpellScaling(mechanics, hero, buffer, tier, spellKey)
	local scaling = hero:getBonuses({type = "SPECIAL_SPELL_SCALING", subtype = spellKey})
	if scaling:size() == 0 then return end

	local percent = scaling:getBonus(1):getVal() * math.floor(hero:getLevel() / tier)
	if percent == 0 then return end
	for _, nb in pairs(buffer) do
		nb.val = math.floor((nb.val or 0) * (100 + percent) / 100)
	end
end

function Script:applyHeroSpecialty(mechanics, buffer, unit)
	local hero = mechanics:getHeroCaster()
	if not hero then return end

	local spellKey = mechanics:getSpell():getJsonKey()
	-- These New Horizons effects have authored power terms and no configured
	-- spell specialty that should rewrite their fixed or derived components.
	if spellKey == HOLY_ARMOR_SPELL or spellKey == HEAVENLY_GALE_SPELL
		or spellKey == GUARDIAN_SPIRIT_SPELL
		or spellKey == DIVINE_RETRIBUTION_SPELL
		or spellKey == CRUSADE_SPELL or spellKey == ENTANGLE_SPELL
		or spellKey == VENGEFUL_VINES_SPELL then return end
	local tier = math.max(unit:creatureLevel(), 1)

	self:applySpellScaling(mechanics, hero, buffer, tier, spellKey)
	self:applyPeculiarEnchant(mechanics, hero, buffer, tier, spellKey)
	self:applyAddValueEnchant(mechanics, hero, buffer, tier, spellKey)
	self:applyFixedValueEnchant(mechanics, hero, buffer, tier, spellKey)
end

function Script:scaleAegisPowerTerm(mechanics, numerator, divisor, coefficientBasisPoints)
	local hero = mechanics:getHeroCaster()
	if hero and hero:hasActivePerk(LIGHT_MAGIC_SKILL, AEGIS_PERK) then
		-- Fold Aegis into the ratio before the shared scaler's final floor so
		-- fractional School/Spellcraft/Warcasting/Empower products are retained.
		numerator = numerator * 120
		divisor = divisor * 100
	end
	return mechanics:scaleSpellPowerComponentWithCoefficientBasisPoints(
		numerator, divisor, coefficientBasisPoints)
end

function Script:applyHolyArmorPower(mechanics, buffer, spellKey)
	if spellKey ~= HOLY_ARMOR_SPELL then return end

	local spellPowerTerm = self:scaleAegisPowerTerm(mechanics,
		mechanics:getEffectPower(), HOLY_ARMOR_SPELL_POWER_DIVISOR,
		mechanics:getSpellPowerCoefficientBasisPoints())

	for _, nb in pairs(buffer) do
		if nb.type == "SPELL_DAMAGE_REDUCTION" then
			-- The fixed 30% base remains unchanged; the common scaler applies the
			-- saved Light/Spellcraft coefficient plus Warcasting and Empower only
			-- to the Spell Power term. Preserve this as one independent source.
			nb.val = math.min(HOLY_ARMOR_MAX_REDUCTION_PERCENT,
				(nb.val or 0) + spellPowerTerm)
		end
	end
end

function Script:applyHeavenlyGalePower(mechanics, buffer, spellKey)
	if spellKey ~= HEAVENLY_GALE_SPELL then return end

	local spellPowerTerm = self:scaleAegisPowerTerm(mechanics,
		HEAVENLY_GALE_SPELL_POWER_COEFFICIENT_BASIS_POINTS * mechanics:getEffectPower(), 1,
		mechanics:getSpellPowerCoefficientBasisPoints())
	local reduction = math.min(HEAVENLY_GALE_MAX_REDUCTION_BASIS_POINTS,
		HEAVENLY_GALE_BASE_REDUCTION_BASIS_POINTS + spellPowerTerm)

	for _, nb in pairs(buffer) do
		if nb.type == "HEAVENLY_GALE" then
			-- Keep the fixed 50% separate from School, Spellcraft, Warcasting,
			-- Empower, and Aegis, all of which scale only the Spell Power term.
			nb.val = reduction
		end
	end
end

function Script:applyShieldOfChaosPower(mechanics, buffer, spellKey)
	if spellKey ~= SHIELD_OF_CHAOS_SPELL or not mechanics:usesNewHorizonsMagicV3() then return end

	local powerTerm = mechanics:scaleSpellPowerComponentWithCoefficientBasisPoints(
		SHIELD_OF_CHAOS_SPELL_POWER_REDUCTION_BASIS_POINTS * math.max(0, mechanics:getEffectPower()), 1,
		mechanics:getSpellPowerCoefficientBasisPoints())
	local reduction = math.min(SHIELD_OF_CHAOS_MAX_REDUCTION_BASIS_POINTS,
		SHIELD_OF_CHAOS_BASE_REDUCTION_BASIS_POINTS + powerTerm)
	local hero = mechanics:getHeroCaster()
	if hero and hero:hasActivePerk(CHAOS_MAGIC_SKILL, PARADOX_SHIELD_PERK) then
		-- Paradox Shield is added after the base formula's 80% cap. The shared physical
		-- damage stage still applies its saved global 80% aggregate cap.
		reduction = reduction + PARADOX_SHIELD_BONUS_BASIS_POINTS
	end

	for _, nb in pairs(buffer) do
		if nb.type == "PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS"
			or nb.type == "SPELL_DAMAGE_REDUCTION_BASIS_POINTS" then
			nb.val = reduction
		end
	end
end

function Script:applyGuardianSpiritPower(mechanics, buffer, spellKey)
	if spellKey ~= GUARDIAN_SPIRIT_SPELL then return end

	local powerTerm = mechanics:scaleSpellPowerComponentWithCoefficientBasisPoints(
		2 * mechanics:getEffectPower(), 1,
		mechanics:getSpellPowerCoefficientBasisPoints())
	local hero = mechanics:getHeroCaster()
	if hero and hero:hasActivePerk(LIGHT_MAGIC_SKILL, HEALER_PERK) then
		powerTerm = math.floor(powerTerm * 120 / 100)
	end
	local pool = 50 + powerTerm
	if hero and hero:hasActivePerk(LIGHT_MAGIC_SKILL, GUARDIAN_PERK) then
		pool = math.floor(pool * 125 / 100)
	end
	for _, nb in pairs(buffer) do
		if nb.type == "GUARDIAN_SPIRIT" then
			-- The timed marker carries the initial pool through SetStackEffect;
			-- the authoritative battle state stores its remaining HP separately.
			nb.val = pool
		end
	end
end

function Script:applyDivineRetributionPower(mechanics, buffer, spellKey)
	if spellKey ~= DIVINE_RETRIBUTION_SPELL then return end

	local spellPowerTerm = mechanics:scaleSpellPowerComponentWithCoefficientBasisPoints(
		DIVINE_RETRIBUTION_POWER_NUMERATOR * math.max(0, mechanics:getEffectPower()),
		DIVINE_RETRIBUTION_POWER_DIVISOR,
		mechanics:getSpellPowerCoefficientBasisPoints())
	local cap = DIVINE_RETRIBUTION_BASE_DAMAGE + spellPowerTerm
	local hero = mechanics:getHeroCaster()
	local retributionistPercent = hero
		and hero:hasActivePerk(LIGHT_MAGIC_SKILL, RETRIBUTIONIST_PERK) and 120 or 100

	for _, nb in pairs(buffer) do
		if nb.type == "DIVINE_RETRIBUTION" then
			-- The fixed 25 stays outside the saved School/Spellcraft/Warcasting/
			-- Empower coefficient. Retributionist is snapshotted separately so its
			-- 20% applies after the end-of-round 30%/cap calculation.
			nb.val = cap
			nb.addInfo = { retributionistPercent = retributionistPercent }
		end
	end
end

function Script:applyCrusadePower(mechanics, buffer, spellKey)
	if spellKey ~= CRUSADE_SPELL or not mechanics:usesNewHorizonsMagicV3() then return end

	local spellPower = math.max(0, mechanics:getEffectPower())
	local coefficient = mechanics:getSpellPowerCoefficientBasisPoints()
	local attributePowerTerm = mechanics:scaleSpellPowerComponentWithCoefficientBasisPoints(
		spellPower, CRUSADE_ATTRIBUTE_POWER_DIVISOR, coefficient)
	local initiativePowerTerm = mechanics:scaleSpellPowerComponentWithCoefficientBasisPoints(
		spellPower, CRUSADE_INITIATIVE_POWER_DIVISOR, coefficient)
	local reductionPowerTerm = mechanics:scaleSpellPowerComponentWithCoefficientBasisPoints(
		CRUSADE_REDUCTION_POWER_BASIS_POINTS_NUMERATOR * spellPower,
		CRUSADE_REDUCTION_POWER_DIVISOR, coefficient)
	local attackAndDefense = math.min(6, CRUSADE_BASE_ATTRIBUTE_BONUS + attributePowerTerm)
	local initiative = math.min(3, CRUSADE_BASE_INITIATIVE_BONUS + initiativePowerTerm)
	local reduction = math.min(CRUSADE_MAX_REDUCTION_BASIS_POINTS,
		CRUSADE_BASE_REDUCTION_BASIS_POINTS + reductionPowerTerm)

	for _, nb in pairs(buffer) do
		if nb.type == "PRIMARY_SKILL" then
			nb.val = attackAndDefense
		elseif nb.type == "STACKS_INITIATIVE_FLAT" then
			nb.val = initiative
		elseif nb.type == "SPELL_DAMAGE_REDUCTION_BASIS_POINTS" then
			nb.val = reduction
		end
	end
end

function Script:describeCrusadeEffect(server, battle, bonuses)
	local attack = bonuses.attack and bonuses.attack.val or 0
	local defense = bonuses.defense and bonuses.defense.val or 0
	local initiative = bonuses.initiative and bonuses.initiative.val or 0
	local reduction = bonuses.magicalDamageReduction and bonuses.magicalDamageReduction.val or 0
	local duration = bonuses.attack and bonuses.attack.turns or CRUSADE_BASE_DURATION
	local percent = math.floor(reduction / 100)
	local fractionalPercent = reduction % 100
	server:appendLog(battle, {
		appendRaw = { string.format(
			"Crusade! grants +%d Attack, +%d Defense, +%d flat Initiative, and %d.%02d%% independent Magical Damage Reduction to the friendly army for %d rounds; affected creatures cannot suffer negative Morale.",
			attack, defense, initiative, percent, fractionalPercent, duration) }
	})
end

function Script:describeShieldOfChaosEffect(server, battle, unit, bonuses)
	local physicalReduction = bonuses.physicalDamageReduction and bonuses.physicalDamageReduction.val or 0
	local magicalReduction = bonuses.magicalDamageReduction and bonuses.magicalDamageReduction.val or 0
	local duration = bonuses.physicalDamageReduction and bonuses.physicalDamageReduction.turns
		or SHIELD_OF_CHAOS_BASE_DURATION
	local targetName = unit:getCreature():getNameTextID(unit:getCount())
	local message = string.format(
		"Shield of Chaos grants %d.%02d%% physical damage reduction before caps (the global physical cap is 80%%) and %d.%02d%% magical damage reduction for %d rounds; it imposes -10 Morale and -10 Luck",
		math.floor(physicalReduction / 100), physicalReduction % 100,
		math.floor(magicalReduction / 100), magicalReduction % 100, duration) .. " on %s."
	server:appendLog(battle, {
		appendRaw = { message },
		replaceStrings = { targetName }
	})
end

function Script:describeEntangleEffect(server, battle, bonuses)
	local duration = ENTANGLE_BASE_DURATION
	for _, bonus in pairs(bonuses) do
		if bonus.type == "BIND_EFFECT" then
			duration = bonus.turns or duration
			break
		end
	end
	server:appendLog(battle, {
		appendRaw = { string.format(
			"Entangle roots a stack for %d round(s). It prevents voluntary movement only; attacks, retaliation, shooting, Wait, Defend, and nonmovement abilities remain available.",
			duration) }
	})
end

function Script:describeVengefulVinesEffect(server, battle, unit, bonuses)
	local duration = VENGEFUL_VINES_BASE_DURATION
	local speedPenalty = 2
	for _, bonus in pairs(bonuses) do
		if bonus.type == "STACKS_MOVEMENT_RANGE" then
			duration = bonus.turns or duration
			speedPenalty = math.abs(bonus.val or -speedPenalty)
			break
		end
	end
	server:appendLog(battle, {
		appendRaw = { "Vengeful Vines reduces the movement Speed of %s by %d for %d rounds; Initiative is unchanged." },
		replaceStrings = { unit:getCreature():getNameTextID(unit:getCount()) },
		replaceNumbers = { speedPenalty, duration }
	})
end

function Script:describeEffect(server, battle, unit, bonuses)
	-- Age spell: STACK_HEALTH bonus with negative val gets a custom message
	for _, nb in pairs(bonuses) do
		if nb.type == "STACK_HEALTH" and (nb.val or 0) < 0 then
			local oldHealth = unit:getMaxHealth()
			local lost = oldHealth - math.floor((oldHealth * (100 + nb.val)) / 100)
			local count = unit:getCount()
			local ageTextID = count == 1 and self.battleLogSingular or self.battleLogPlural
			server:appendLog(battle, {
				append         = { ageTextID },
				replaceStrings = { unit:getCreature():getNameTextID(count) },
				replaceNumbers = { lost }
			})
			return
		end
	end

	if not self.battleLogPlural or self.battleLogPlural == "" then return end

	local textID = (self.battleLogSingular and self.battleLogSingular ~= "" and unit:getCount() == 1) and self.battleLogSingular or self.battleLogPlural
	local nameTextID = unit:getCreature():getNameTextID(unit:getCount())
	server:appendLog(battle, {
		append         = { textID },
		replaceStrings = { nameTextID }
	})
end

function Script:apply(mechanics, server, target)
	local battle   = mechanics:getBattle()
	local describe = server:describeChanges()
	local spellKey = mechanics:getSpell():getJsonKey()
	if spellKey == CRUSADE_SPELL and not mechanics:usesNewHorizonsMagicV3() then return end
	if spellKey == SHIELD_OF_CHAOS_SPELL and not mechanics:usesNewHorizonsMagicV3() then return end
	if spellKey == ENTANGLE_SPELL and not mechanics:usesNewHorizonsMagicV3() then return end
	if spellKey == VENGEFUL_VINES_SPELL and not mechanics:usesNewHorizonsMagicV3() then return end
	local converted = self:convertBonuses(mechanics)
	local describedCrusade = false
	local describedShieldOfChaos = false

	for _, dest in ipairs(target) do
		local unit = dest.unit
		if not unit or not unit:isAlive() then goto continue end

		local buffer = {}
		for name, nb in pairs(converted) do
			buffer[name] = self:deepCopyBonus(nb)
		end

		self:applyHeroSpecialty(mechanics, buffer, unit)
		self:applyHolyArmorPower(mechanics, buffer, mechanics:getSpell():getJsonKey())
		self:applyHeavenlyGalePower(mechanics, buffer, mechanics:getSpell():getJsonKey())
		self:applyShieldOfChaosPower(mechanics, buffer, mechanics:getSpell():getJsonKey())
		self:applyGuardianSpiritPower(mechanics, buffer, mechanics:getSpell():getJsonKey())
		self:applyDivineRetributionPower(mechanics, buffer, mechanics:getSpell():getJsonKey())
		self:applyCrusadePower(mechanics, buffer, spellKey)
		self:applyTemporalFieldScale(mechanics, buffer, mechanics:getSpell():getJsonKey())

		if describe then
			if spellKey == CRUSADE_SPELL then
				if not describedCrusade then
					self:describeCrusadeEffect(server, battle, buffer)
					describedCrusade = true
				end
			elseif spellKey == SHIELD_OF_CHAOS_SPELL then
				if not describedShieldOfChaos then
					self:describeShieldOfChaosEffect(server, battle, unit, buffer)
					describedShieldOfChaos = true
				end
			elseif spellKey == ENTANGLE_SPELL then
				self:describeEntangleEffect(server, battle, buffer)
			elseif spellKey == VENGEFUL_VINES_SPELL then
				self:describeVengefulVinesEffect(server, battle, unit, buffer)
			else
				self:describeEffect(server, battle, unit, buffer)
			end
		end

		if spellKey == DIVINE_RETRIBUTION_SPELL then
			local previous = unit:getBonuses({ type = "DIVINE_RETRIBUTION" }):filter(function(bonus)
				return bonus:getSource() == ENUM.BonusSource.spellEffect
					and bonus:getSourceID() == spellKey
			end)
			if previous:size() > 0 then
				server:removeUnitBonuses(battle, unit, previous)
			end
		end

		for _, nb in pairs(buffer) do
			server:addUnitBonus(battle, unit, nb, self.cumulative or false)
		end

		::continue::
	end
end

return Script
