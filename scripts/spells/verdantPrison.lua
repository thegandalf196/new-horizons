local Base = require("spells/spellEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local SPELL_ID = "new-horizons:verdantPrison"
local NATURE_MAGIC_SKILL = "new-horizons:natureMagic"
local VERDANT_WARDEN_PERK = "new-horizons:natureMagic.verdantWarden"
local DENDROID_GUARD_ID = "core:dendroidGuard"
local BASE_HEALTH_POOL = 180
local SPELL_POWER_HEALTH_COEFFICIENT = 3
local VERDANT_WARDEN_PERCENT = 125

local function reject(problem, mechanics)
	if problem then
		problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
	end
	return false
end

local function usesCanonicalRules(mechanics)
	return mechanics:getSpell():getJsonKey() == SPELL_ID
		and mechanics:usesNewHorizonsMagicV3()
end

local function isLegalAnchor(mechanics, unit)
	if unit == nil or not unit:isAlive() or not unit:isValidTarget(false)
		or unit:isInvincible() or not mechanics:isReceptive(unit) then
		return false
	end

	if mechanics:isMagicMirror() then
		return unit:getSide() == mechanics:getCasterSide()
	end
	return unit:getSide() ~= mechanics:getCasterSide()
end

local function liveUnitById(mechanics, candidate)
	if candidate == nil then return nil end
	local id = candidate:unitID()
	local units = mechanics:getBattle():getUnitsIf(function(unit)
		return unit:unitID() == id
	end)
	return units[1]
end

local function unitCoveringHex(mechanics, hex)
	if hex == nil or not hex:isValid() then return nil end

	local units = mechanics:getBattle():getUnitsIf(function(unit)
		return unit:isAlive() and unit:coversPos(hex)
	end)
	return units[1]
end

local function unitFromDestinations(mechanics, destinations)
	if destinations == nil then return nil end

	-- If a destination carries a unit identity, resolve that identity in the
	-- current battle first and fail closed if it went stale. Hex-only hover/range
	-- queries still resolve either wide footprint cell.
	local hasUnitIdentity = false
	for _, destination in ipairs(destinations) do
		if destination.unit ~= nil then
			hasUnitIdentity = true
			local unit = liveUnitById(mechanics, destination.unit)
			if unit ~= nil then return unit end
		end
	end
	if hasUnitIdentity then return nil end
	for _, destination in ipairs(destinations) do
		local unit = unitCoveringHex(mechanics, destination.hex)
		if unit ~= nil then return unit end
	end
	return nil
end

local function targetFromAim(mechanics, aimPoint, spellTarget)
	if spellTarget ~= nil and #spellTarget > 0 then
		return unitFromDestinations(mechanics, spellTarget)
	end
	return unitFromDestinations(mechanics, aimPoint)
end

local function hexKey(hex)
	return tostring(hex:getY()) .. ":" .. tostring(hex:getX())
end

local function sortHexesByNumericIndex(hexes)
	-- BattleHex indices are x + y * field-width, so row then column is numeric
	-- battlefield order and also makes remainder assignment deterministic.
	table.sort(hexes, function(left, right)
		if left:getY() ~= right:getY() then
			return left:getY() < right:getY()
		end
		return left:getX() < right:getX()
	end)
	return hexes
end

local function legalRing(mechanics, unit)
	if not isLegalAnchor(mechanics, unit) then return {} end

	local battle = mechanics:getBattle()
	local dendroidGuard = LIBRARY:getCreatureByName(DENDROID_GUARD_ID)
	local seen = {}
	local ring = {}
	local surrounding = unit:getSurroundingHexes()

	for index = 1, surrounding:size() do
		local hex = surrounding:at(index)
		if hex:isAvailable() then
			local available = battle:getAvailableHex(dendroidGuard, mechanics:getCasterSide(), hex)
			if available:isValid() and available == hex then
				local key = hexKey(hex)
				if not seen[key] then
					seen[key] = true
					ring[#ring + 1] = hex
				end
			end
		end
	end

	return sortHexesByNumericIndex(ring)
end

local function targetFromCanonical(mechanics, target)
	if target == nil or #target ~= 1 then return nil end
	return unitFromDestinations(mechanics, target)
end

local function healthPool(mechanics)
	local spellPower = math.max(0, mechanics:getEffectPower())
	local hero = mechanics:getHeroCaster()
	local modifierPercent = 100
	if hero ~= nil and hero:hasActivePerk(NATURE_MAGIC_SKILL, VERDANT_WARDEN_PERK) then
		modifierPercent = VERDANT_WARDEN_PERCENT
	end

	-- Keep fractional School/Spellcraft/Warcasting/Empower scaling until the
	-- single final whole-HP floor. Multiplying by the whole-pool modifier first
	-- also applies Verdant Warden to the fixed 180 base.
	local spellPowerHundredths = mechanics:scaleSpellPowerComponentWithCoefficientBasisPoints(
		SPELL_POWER_HEALTH_COEFFICIENT * spellPower * modifierPercent,
		1,
		mechanics:getSpellPowerCoefficientBasisPoints())
	return math.floor((BASE_HEALTH_POOL * modifierPercent + spellPowerHundredths) / 100)
end

local function allocation(mechanics, ring, pool)
	if #ring == 0 or pool <= 0 then return {}, 0 end

	local creature = LIBRARY:getCreatureByName(DENDROID_GUARD_ID)
	local maxHealth = mechanics:getSummonedCreatureMaxHealth(creature, true)
	if maxHealth <= 0 then return {}, 0 end

	local baseShare = math.floor(pool / #ring)
	local remainder = pool % #ring
	local shares = {}
	local totalCreatures = 0
	for index, hex in ipairs(ring) do
		local share = baseShare + (index <= remainder and 1 or 0)
		if share > 0 then
			local count = math.ceil(share / maxHealth)
			shares[#shares + 1] = {hex = hex, health = share, count = count}
			totalCreatures = totalCreatures + count
		end
	end
	return shares, totalCreatures
end

function Script:applicableGeneral(mechanics, problem)
	if not usesCanonicalRules(mechanics) or mechanics:isMagicMirror() then
		return reject(problem, mechanics)
	end

	local enemies = mechanics:getBattle():getUnitsIf(function(unit)
		return unit:getSide() ~= mechanics:getCasterSide()
			and #legalRing(mechanics, unit) > 0
	end)
	if #enemies == 0 then return reject(problem, mechanics) end
	return true
end

function Script:applicableTarget(mechanics, problem, target)
	if not usesCanonicalRules(mechanics) then
		return reject(problem, mechanics)
	end

	local unit = targetFromCanonical(mechanics, target)
	if not isLegalAnchor(mechanics, unit) or #legalRing(mechanics, unit) == 0 then
		return reject(problem, mechanics)
	end
	return true
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	if not usesCanonicalRules(mechanics) then return {} end

	local unit = targetFromAim(mechanics, aimPoint, spellTarget)
	if not isLegalAnchor(mechanics, unit) or #legalRing(mechanics, unit) == 0 then
		return {}
	end

	-- Keep the real unit as the effect destination. The normal spell pipeline
	-- must still see it for Magic Resistance, Spell Lock and Magic Mirror. The
	-- ring itself is independently shared by preview and apply below.
	return {{unit = unit, hex = unit:getPosition()}}
end

function Script:adjustAffectedHexes(mechanics, hexes, spellTarget)
	if not usesCanonicalRules(mechanics) then return hexes end

	local unit = unitFromDestinations(mechanics, spellTarget)
	local ring = legalRing(mechanics, unit)
	for index = hexes:size(), 1, -1 do
		hexes:erase(hexes:at(index))
	end
	for _, hex in ipairs(ring) do
		hexes:insert(hex)
	end
	return hexes
end

function Script:getHealthChange(mechanics, spellTarget)
	if not usesCanonicalRules(mechanics) then
		return {hpDelta = 0, unitsDelta = 0}
	end

	local unit = targetFromCanonical(mechanics, spellTarget)
	local ring = legalRing(mechanics, unit)
	local pool = healthPool(mechanics)
	local _, totalCreatures = allocation(mechanics, ring, pool)
	if #ring == 0 or totalCreatures == 0 then
		return {hpDelta = 0, unitsDelta = 0}
	end
	return {
		hpDelta = pool,
		unitsDelta = totalCreatures,
		unitType = LIBRARY:getCreatureByName(DENDROID_GUARD_ID)
	}
end

function Script:apply(mechanics, server, target)
	if not usesCanonicalRules(mechanics) then return end

	local unit = targetFromCanonical(mechanics, target)
	local ring = legalRing(mechanics, unit)
	local pool = healthPool(mechanics)
	local shares = allocation(mechanics, ring, pool)
	if #shares == 0 then return end

	local battle = mechanics:getBattle()
	local creature = LIBRARY:getCreatureByName(DENDROID_GUARD_ID)
	for _, share in ipairs(shares) do
		local summoned = server:addUnit(battle, {
			count = share.count,
			type = creature,
			side = mechanics:getCasterSide(),
			position = share.hex,
			summoned = true,
			natureSummoned = true
		})
		if summoned ~= nil then
			local excess = summoned:getAvailableHealth() - share.health
			if excess > 0 then
				local state = summoned:copy()
				state:damage(excess)
				server:changeUnit(battle, state)
			end
		end
	end
end

return Script
