local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local HOLY_ARMOR_SPELL = "new-horizons:holyArmor"
local HEAVENLY_GALE_SPELL = "new-horizons:heavenlyGale"
local GUARDIAN_SPIRIT_SPELL = "new-horizons:guardianSpirit"
local DIVINE_RETRIBUTION_SPELL = "new-horizons:divineRetribution"
local LIGHT_MAGIC_SKILL = "new-horizons:lightMagic"
local HEALER_PERK = "new-horizons:lightMagic.healer"
local GUARDIAN_PERK = "new-horizons:lightMagic.guardian"
local AEGIS_PERK = "new-horizons:lightMagic.aegis"
local RETRIBUTIONIST_PERK = "new-horizons:lightMagic.retributionist"
local HOLY_ARMOR_SPELL_POWER_DIVISOR = 5
local HOLY_ARMOR_MAX_REDUCTION_PERCENT = 60
local HEAVENLY_GALE_BASE_REDUCTION_BASIS_POINTS = 5000
local HEAVENLY_GALE_SPELL_POWER_COEFFICIENT_BASIS_POINTS = 15
local HEAVENLY_GALE_MAX_REDUCTION_BASIS_POINTS = 8000
local DIVINE_RETRIBUTION_BASE_DAMAGE = 25
local DIVINE_RETRIBUTION_POWER_NUMERATOR = 125
local DIVINE_RETRIBUTION_POWER_DIVISOR = 100
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
	local duration = mechanics:getEffectDuration()
	local spellKey = mechanics:getSpell():getJsonKey()
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
		if not nb.turns or nb.turns == 0 then
			nb.turns = duration
		end

		nb.sourceType = "SPELL_EFFECT"
		nb.sourceID = spellKey

		converted[name] = nb
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
		or spellKey == DIVINE_RETRIBUTION_SPELL then return end
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
	local converted = self:convertBonuses(mechanics)
	local spellKey = mechanics:getSpell():getJsonKey()

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
		self:applyGuardianSpiritPower(mechanics, buffer, mechanics:getSpell():getJsonKey())
		self:applyDivineRetributionPower(mechanics, buffer, mechanics:getSpell():getJsonKey())
		self:applyTemporalFieldScale(mechanics, buffer, mechanics:getSpell():getJsonKey())

		if describe then
			self:describeEffect(server, battle, unit, buffer)
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
