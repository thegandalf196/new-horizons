local Base = require("spells/unitEffect")
local BattleLog = require("battleLog")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local HAVOC_CONDUCTOR_SKILL = "new-horizons:havocMagic"
local HAVOC_CONDUCTOR_PERK = "new-horizons:havocMagic.conductor"

local function conductorMultiplier(mechanics, targetIndex)
	if targetIndex <= 0 then return nil end
	local hero = mechanics:getHeroCaster()
	local spell = mechanics:getSpell()
	if not hero or not spell or not hero:hasActivePerk(HAVOC_CONDUCTOR_SKILL, HAVOC_CONDUCTOR_PERK) then
		return nil
	end
	local key = spell:getJsonKey()
	if key ~= "core:chainLightning" and key ~= "new-horizons:masterChainLightning" then
		return nil
	end
	-- Conductor's values are multipliers from the initial hit, rather than
	-- another geometric per-hop factor.  The final entry also covers the
	-- fifth target of Master Chain Lightning.
	local multipliers = { 1.00, 0.75, 0.55, 0.40, 0.30 }
	-- targetIndex zero is the initial target, so Lua's one-based table index is
	-- the battle target index plus one: 100% / 75% / 55% / 40% / 30%.
	return multipliers[targetIndex + 1]
end

function Script:isReceptive(mechanics, unit)
	local spell = mechanics:getSpell()
	if spell:isMagical() then
		if unit:getBonusesValue({type = "SPELL_DAMAGE_REDUCTION", subtype = "any"}) >= 100 then
			return false
		end
	end
	local reductions = unit:getBonuses({type = "SPELL_DAMAGE_REDUCTION"})
	for _, school in ipairs(spell:getSchools()) do
		-- the subtype of each names a school, and telling apart the school it names from the "any"
		-- that covers all of them is not something the filter can ask for
		local matching = reductions:filter(function(b)
			local sub = b:getSubtype()
			if sub == "any" then return false end
			return LIBRARY:getSpellSchoolByName(sub) == school
		end)
		local total = 0
		for i = 1, matching:size() do
			total = total + matching:getBonus(i):getVal()
		end
		if total >= 100 then
			return false
		end
	end
	return Base.isReceptive(self, mechanics, unit)
end

function Script:damageForTarget(targetIndex, mechanics, unit)
	local base
	if self.killByPercentage then
		local toKill = math.floor(unit:getCount() * mechanics:getEffectValue() / 100)
		base = toKill * unit:getMaxHealth()
	elseif self.killByCount then
		base = mechanics:getEffectValue() * unit:getMaxHealth()
	else
		base = mechanics:adjustEffectValue(unit)
	end
	local chainLength = self.chainLength or 0
	if chainLength > 1 and targetIndex > 0 then
		local chainFactor = self.chainFactor or 0
		local factorPerHeroLevel = self.chainFactorPerHeroLevel or 0
		if factorPerHeroLevel ~= 0 then
			local hero = mechanics:getHeroCaster()
			local heroLevel = hero and hero:getLevel() or 1
			chainFactor = chainFactor + factorPerHeroLevel * math.max(0, heroLevel)
			if self.chainFactorMaximum then
				chainFactor = math.min(chainFactor, self.chainFactorMaximum)
			end
		end
		local multiplier = chainFactor ^ targetIndex
		local conductor = conductorMultiplier(mechanics, targetIndex)
		-- Conductor replaces ordinary Chain Lightning's worse falloff, but it
		-- must never erase Master Chain Lightning's level-scaled specialty.
		-- When both apply, retain the better multiplier for this jump.
		if conductor then
			multiplier = math.max(multiplier, conductor)
		end
		base = math.floor(multiplier * base)
	end
	return base
end

function Script:getHealthChange(mechanics, spellTarget)
	local result = { hpDelta = 0, unitsDelta = 0 }
	for i, dest in ipairs(spellTarget) do
		local unit = dest.unit
		if unit and unit:isAlive() then
			local amount = self:damageForTarget(i - 1, mechanics, unit)
			local copy = unit:copy()
			local hpBefore    = copy:getAvailableHealth()
			local countBefore = copy:getCount()
			copy:damage(amount)
			result.hpDelta    = result.hpDelta    - (hpBefore    - copy:getAvailableHealth())
			result.unitsDelta = result.unitsDelta - (countBefore - copy:getCount())
		end
	end
	return result
end

function Script:apply(mechanics, server, target)
	local battle   = mechanics:getBattle()
	local describe = server:describeChanges()
	local firstUnit, totalDamage, totalKilled, multiple = nil, 0, 0, false

	for i, dest in ipairs(target) do
		local unit = dest.unit
		if unit and unit:isAlive() then
			local amount = self:damageForTarget(i - 1, mechanics, unit)
			-- Creature casts expose their battle Unit; hero and environmental casts return nil and
			-- intentionally remain unattributed.
			local dmg, killed = server:damageUnit(
				battle, unit, amount, self.destroyRemains == true, mechanics:getUnitCaster())
			if describe then
				if firstUnit then multiple = true else firstUnit = unit end
				totalDamage = totalDamage + dmg
				totalKilled = totalKilled + killed
			end
		end
	end

	if describe and firstUnit and totalDamage > 0 then
		self:describeEffect(server, battle, mechanics, firstUnit, totalKilled, totalDamage, multiple)
	end
end

function Script:describeEffect(server, battle, mechanics, firstUnit, kills, damage, multiple)
	local spell    = mechanics:getSpell()
	local spellKey = spell:getJsonKey()

	if spellKey:find("deathStare") and not multiple then
		local casterNameID = mechanics:getCasterNameTextID()
		if kills > 1 then
			server:appendLog(battle, {
				append         = { "core.genrltxt.119" },
				replaceStrings = { firstUnit:getCreature():getNameTextID(0), casterNameID },
				replaceNumbers = { kills }
			})
		else
			server:appendLog(battle, {
				append         = { "core.genrltxt.118" },
				replaceStrings = { firstUnit:getCreature():getNameTextID(1), casterNameID }
			})
		end

	elseif spellKey:find("accurateShot") and not multiple then
		local textID = mechanics:getPluralFormTextID(
			"vcmi.battleWindow.accurateShot.resultDescription", kills)
		server:appendLog(battle, {
			append         = { textID },
			replaceStrings = { firstUnit:getCreature():getNameTextID(kills) },
			replaceNumbers = { kills }
		})

	elseif spellKey:find("thunderbolt") and not multiple then
		server:appendLog(battle, {
			append         = { "core.genrltxt.367" },
			replaceStrings = { firstUnit:getCreature():getNameTextID(0) }
		})
		server:appendLog(battle, {
			append         = { "core.genrltxt.343" },
			replaceNumbers = { damage }
		})

	else
		-- an area spell kills creatures of several stacks, so the log names none of them
		BattleLog.spellDamage(server, battle, spell, not multiple and firstUnit or nil, damage, kills)
	end
end

return Script
