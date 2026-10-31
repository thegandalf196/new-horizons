local Base = require("spells/unitEffect")
local SorceryRevision = require("spells/sorceryRevision")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local HOLY_ARMOR_SPELL = "new-horizons:holyArmor"
local HEAVENLY_GALE_SPELL = "new-horizons:heavenlyGale"
local GUARDIAN_SPIRIT_SPELL = "new-horizons:guardianSpirit"
local DIVINE_RETRIBUTION_SPELL = "new-horizons:divineRetribution"
local CRUSADE_SPELL = "new-horizons:crusade"
local SANCTUARY_SPELL = "new-horizons:sanctuary"
local ENTANGLE_SPELL = "new-horizons:entangle"
local VENGEFUL_VINES_SPELL = "new-horizons:vengefulVines"
local MISFORTUNE_SPELL = "core:misfortune"
local FORGETFULNESS_SPELL = "core:forgetfulness"
local MINDBREAKER_PERK = "new-horizons:chaosMagic.mindbreaker"
local LIGHT_MAGIC_SKILL = "new-horizons:lightMagic"
local HEALER_PERK = "new-horizons:lightMagic.healer"
local GUARDIAN_PERK = "new-horizons:lightMagic.guardian"
local RETRIBUTIONIST_PERK = "new-horizons:lightMagic.retributionist"
local CRUSADER_PERK = "new-horizons:lightMagic.crusader"
local SANCTUARY_KEEPER_PERK = "new-horizons:lightMagic.sanctuaryKeeper"
local NATURE_MAGIC_SKILL = "new-horizons:natureMagic"
local CHAOS_MAGIC_SKILL = "new-horizons:chaosMagic"
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
local VENGEFUL_VINES_BASE_DURATION = 2
local SHIELD_OF_CHAOS_SPELL = "new-horizons:shieldOfChaos"
local SHIELD_OF_CHAOS_BASE_DURATION = 2
local SLOW_SPELL = "core:slow"

function Script:deepCopyBonus(b)
	local copy = {}
	for k, v in pairs(b) do
		copy[k] = v
	end
	return copy
end

function Script:convertBonuses(mechanics)
	local spellKey = mechanics:getEffectSpell():getJsonKey()
	local duration = nil
	local crusadeDuration = nil
	local entangleDuration = nil
	local vengefulVinesDuration = nil
	local misfortuneDuration = nil
	local forgetfulnessDuration = nil
	local shieldOfChaosDuration = nil
	local misfortuneChanceMultiplierBasisPoints = nil
	if spellKey == FORGETFULNESS_SPELL and mechanics:usesNewHorizonsMagicV3() then
		forgetfulnessDuration = mechanics:getForgetfulnessDuration()
	elseif spellKey == SHIELD_OF_CHAOS_SPELL and mechanics:usesNewHorizonsMagicV3() then
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
		entangleDuration = mechanics:getEntangleDuration()
	elseif spellKey == VENGEFUL_VINES_SPELL and mechanics:usesNewHorizonsMagicV3() then
		-- Vengeful Vines has a fixed two-round Speed penalty. Apply only the
		-- common cast-specific Echoed Duration extension to that literal base.
		vengefulVinesDuration = mechanics:adjustEffectDuration(VENGEFUL_VINES_BASE_DURATION)
	elseif spellKey == MISFORTUNE_SPELL and mechanics:usesNewHorizonsMagicV3() then
		misfortuneDuration = mechanics:getMisfortuneDuration()
		misfortuneChanceMultiplierBasisPoints = mechanics:getMisfortuneChanceMultiplier()
	else
		duration = mechanics:getEffectDuration()
	end
	local newHorizonsSlowReduction = nil
	if spellKey == SLOW_SPELL and mechanics:usesNewHorizonsMagicV3() then
		newHorizonsSlowReduction = mechanics:getSlowBaseReduction()
	end
	local converted = {}

	for name, b in pairs(self.bonus or {}) do
		local nb = self:deepCopyBonus(b)
		if forgetfulnessDuration ~= nil and nb.type == "FORGETFULL" then
			-- Canonical Forgetfulness forbids shooting at every School rank.
			-- Legacy casts retain their authored half-damage behavior.
			nb.val = 100
		end
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
		if forgetfulnessDuration ~= nil then
			nb.turns = forgetfulnessDuration
		elseif misfortuneDuration ~= nil then
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
		elseif nb.duration == "N_TURNS" or nb.duration == ENUM.BonusDuration.nTurns then
			nb.turns = nb.turns + mechanics:getExtendSpellBonusRounds()
		end

		nb.sourceType = "SPELL_EFFECT"
		nb.sourceID = spellKey

		converted[name] = nb
	end

	if forgetfulnessDuration ~= nil then
		local suppressionLevel = 1
		local hero = mechanics:getHeroCaster()
		if hero and hero:hasActivePerk(CHAOS_MAGIC_SKILL, MINDBREAKER_PERK) then
			suppressionLevel = 2
		end
		converted.creatureAbilitySuppression = {
			type = "CREATURE_ABILITY_SUPPRESSION",
			duration = ENUM.BonusDuration.nTurns,
			val = suppressionLevel,
			valueType = "INDEPENDENT_MAX",
			turns = forgetfulnessDuration,
			sourceType = "SPELL_EFFECT",
			sourceID = spellKey,
			statusTags = {"DEBUFF"},
			statusIdentity = "core:forgetfulness"
		}
	end

	if misfortuneChanceMultiplierBasisPoints ~= nil then
		converted.favorableCreatureChanceMultiplier = {
			type = "FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS",
			duration = ENUM.BonusDuration.nTurns,
			val = misfortuneChanceMultiplierBasisPoints,
			valueType = "INDEPENDENT_MIN",
			turns = misfortuneDuration,
			sourceType = "SPELL_EFFECT",
			sourceID = spellKey,
			statusTags = {"DEBUFF"},
			statusIdentity = "core:misfortune"
		}
	end

	if spellKey == SLOW_SPELL then SorceryRevision.addSlowSpeed(mechanics, converted) end
	return converted
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

	local spellKey = mechanics:getEffectSpell():getJsonKey()
	-- Capability levels are categorical, not enchantment magnitudes. Legacy
	-- specialty arithmetic must not turn a full shooting ban back into a penalty
	-- or upgrade the base restriction into Mindbreaker's passive suppression.
	if spellKey == FORGETFULNESS_SPELL and mechanics:usesNewHorizonsMagicV3() then return end
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
	for _, nb in pairs(buffer) do
		local override = mechanics:applyCasterEnchantValue(nb.val or 0)
		if override ~= nil then nb.val = override end
	end
end

function Script:applyHolyArmorPower(mechanics, buffer, spellKey, unit)
	if spellKey ~= HOLY_ARMOR_SPELL then return end

	for _, nb in pairs(buffer) do
		if nb.type == "SPELL_DAMAGE_REDUCTION" then
			-- The configured fixed base remains loader-owned; shared C++ arithmetic
			-- preserves the original Aegis/recipient ratios and final cap.
			nb.val = mechanics:getHolyArmorReduction(nb.val or 0, unit)
		end
	end
end

function Script:applyHeavenlyGalePower(mechanics, buffer, spellKey, unit)
	if spellKey ~= HEAVENLY_GALE_SPELL then return end

	local reduction = mechanics:getHeavenlyGaleReductionBasisPoints(unit)

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

	local reduction = mechanics:getShieldOfChaosReduction()

	for _, nb in pairs(buffer) do
		if nb.type == "PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS"
			or nb.type == "SPELL_DAMAGE_REDUCTION_BASIS_POINTS" then
			nb.val = reduction
		end
	end
end

function Script:applyGuardianSpiritPower(mechanics, buffer, spellKey, unit)
	if spellKey ~= GUARDIAN_SPIRIT_SPELL then return end
	local pool = mechanics:getGuardianSpiritHitPoints(unit)
	for _, nb in pairs(buffer) do
		if nb.type == "GUARDIAN_SPIRIT" then
			-- The timed marker carries the initial pool through SetStackEffect;
			-- the authoritative battle state stores its remaining HP separately.
			nb.val = pool
		end
	end
end

function Script:applyDivineRetributionPower(mechanics, buffer, spellKey, unit)
	if spellKey ~= DIVINE_RETRIBUTION_SPELL then return end

	local cap = mechanics:getDivineRetributionCap(unit)
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

function Script:applyCrusadePower(mechanics, buffer, spellKey, unit)
	if spellKey ~= CRUSADE_SPELL or not mechanics:usesNewHorizonsMagicV3() then return end

	local spellPower = math.max(0, mechanics:getEffectPower())
	local attributePowerTerm = mechanics:scaleRecipientSpellPowerComponent(
		spellPower, CRUSADE_ATTRIBUTE_POWER_DIVISOR, unit)
	local initiativePowerTerm = mechanics:scaleRecipientSpellPowerComponent(
		spellPower, CRUSADE_INITIATIVE_POWER_DIVISOR, unit)
	local reductionPowerTerm = mechanics:scaleRecipientSpellPowerComponent(
		CRUSADE_REDUCTION_POWER_BASIS_POINTS_NUMERATOR * spellPower,
		CRUSADE_REDUCTION_POWER_DIVISOR, unit)
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
	local spellKey = mechanics:getEffectSpell():getJsonKey()
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

		if spellKey == SANCTUARY_SPELL then
			-- Recasting Sanctuary replaces its Keeper morale sibling even when the
			-- new cast is not made by a Keeper. This prevents an older +2 from
			-- becoming active again on a later non-Keeper Sanctuary cast.
			local previousMorale = unit:getBonuses({ type = "MORALE" }):filter(function(bonus)
				return bonus:getSource() == ENUM.BonusSource.spellEffect
					and bonus:getSourceID() == spellKey
			end)
			if previousMorale:size() > 0 then
				server:removeUnitBonuses(battle, unit, previousMorale)
			end
		end

		local buffer = {}
		for name, nb in pairs(converted) do
			buffer[name] = self:deepCopyBonus(nb)
		end

		local hero = mechanics:getHeroCaster()
		if spellKey == SANCTUARY_SPELL and mechanics:usesNewHorizonsMagicV3()
			and hero and hero:hasActivePerk(LIGHT_MAGIC_SKILL, SANCTUARY_KEEPER_PERK) then
			buffer.sanctuaryKeeperMorale = {
				type = "MORALE",
				val = 2,
				duration = ENUM.BonusDuration.oneBattle,
				sourceType = ENUM.BonusSource.spellEffect,
				sourceID = spellKey,
				limiters = {
					{ type = "HAS_ANOTHER_BONUS_LIMITER", bonusType = "SANCTIFIED",
						bonusSourceType = "SPELL_EFFECT", bonusSourceID = spellKey }
				}
			}
		end

		self:applyHeroSpecialty(mechanics, buffer, unit)
		self:applyHolyArmorPower(mechanics, buffer, spellKey, unit)
		self:applyHeavenlyGalePower(mechanics, buffer, spellKey, unit)
		self:applyShieldOfChaosPower(mechanics, buffer, spellKey)
		self:applyGuardianSpiritPower(mechanics, buffer, spellKey, unit)
		self:applyDivineRetributionPower(mechanics, buffer, spellKey, unit)
		self:applyCrusadePower(mechanics, buffer, spellKey, unit)
		SorceryRevision.scaleSlow(mechanics, buffer, spellKey)

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

		SorceryRevision.refreshSlow(mechanics, server, battle, unit, spellKey)
		local refreshedType = nil
		if spellKey == DIVINE_RETRIBUTION_SPELL then
			refreshedType = "DIVINE_RETRIBUTION"

		elseif spellKey == "core:bless" and mechanics:usesNewHorizonsMagicV3() then
			-- Ordinary Bless and its perk-granted Mass entry share one status.
			-- Replace the family marker explicitly, including detached AI units.
			refreshedType = "ALWAYS_MAXIMUM_DAMAGE"
		elseif spellKey == "new-horizons:regeneration" and mechanics:usesNewHorizonsMagicV3() then
			-- Keep normal and Mass Regeneration on one wound-tracking marker.
			refreshedType = "HP_REGENERATION"
		end
		if refreshedType then
			local previous = unit:getBonuses({ type = refreshedType }):filter(function(bonus)
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
