local Base = require("spells/spellEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local SPELL_KEY = "new-horizons:timeStop"
local CHRONOMANCER_SKILL = "new-horizons:sorceryMagic"
local CHRONOMANCER_PERK = "new-horizons:sorceryMagic.chronomancer"
local BASE_RADIUS_CAP = 2
local CHRONOMANCER_RADIUS_CAP = 3
local POWER_PER_EXTRA_RADIUS = 100

local DIRECTIONS = {
	"copyToNorthWest", "copyToNorthEast", "copyToEast",
	"copyToSouthEast", "copyToSouthWest", "copyToWest"
}

local function radius(mechanics)
	local hero = mechanics:getHeroCaster()
	local chronomancer = hero ~= nil
		and hero:hasActivePerk(CHRONOMANCER_SKILL, CHRONOMANCER_PERK)
	local cap = chronomancer and CHRONOMANCER_RADIUS_CAP or BASE_RADIUS_CAP
	return math.min(cap, 1 + math.floor(mechanics:getEffectPower() / POWER_PER_EXTRA_RADIUS))
end

local function hexKey(hex)
	return tostring(hex:getX()) .. ":" .. tostring(hex:getY())
end

local function area(center, range)
	local result = {}
	local seen = {[hexKey(center)] = true}
	local queue = {{hex = center, distance = 0}}
	local cursor = 1
	while cursor <= #queue do
		local entry = queue[cursor]
		cursor = cursor + 1
		if entry.hex:isValid() then result[#result + 1] = entry.hex end
		if entry.distance < range then
			for _, direction in ipairs(DIRECTIONS) do
				local nextHex = entry.hex[direction](entry.hex)
				local key = hexKey(nextHex)
				if nextHex:isValid() and not seen[key] then
					seen[key] = true
					queue[#queue + 1] = {hex = nextHex, distance = entry.distance + 1}
				end
			end
		end
	end
	return result
end

local function containsHex(hexes, unit)
	local occupied = unit:getHexes()
	for index = 1, occupied:size() do
		for _, affected in ipairs(hexes) do
			if occupied:at(index) == affected then return true end
		end
	end
	return false
end

local function centerFrom(target)
	return target[1] and target[1].hex or nil
end

function Script:applicableGeneral(mechanics, problem)
	-- A location spell can be selected before a unit is under the cursor.  The
	-- final center and affected units are validated in applicableTarget.
	return true
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	local center = centerFrom(aimPoint)
	if center == nil or not center:isValid() then return {} end
	local affectedHexes = area(center, radius(mechanics))
	local result = {}
	for _, unit in ipairs(mechanics:getBattle():getUnitsIf(function(candidate)
		return candidate:isAlive() and containsHex(affectedHexes, candidate)
	end)) do
		result[#result + 1] = {unit = unit, hex = unit:getPosition()}
	end
	return result
end

function Script:applicableTarget(mechanics, problem, target)
	if #target == 0 then
		problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
		return false
	end
	return true
end

function Script:adjustAffectedHexes(mechanics, hexes, spellTarget)
	local center = centerFrom(spellTarget)
	if center == nil or not center:isValid() then return hexes end
	for _, affected in ipairs(area(center, radius(mechanics))) do
		hexes:insert(affected)
	end
	return hexes
end

function Script:apply(mechanics, server, target)
	local battle = mechanics:getBattle()
	local duration = 1
	for _, destination in ipairs(target) do
		local unit = destination.unit
		if unit ~= nil and unit:isAlive() then
			-- These two standard state bonuses make a stack immediately inactive
			-- and immune to damage/targeting.  The authoritative battle hook still
			-- has to bind expiry to the caster's next Hero Action and pause existing
			-- timed effects; a normal N_TURNS bonus is only the safe fallback.
			server:addUnitBonus(battle, unit, {
				type = "NOT_ACTIVE",
				val = 1,
				duration = ENUM.BonusDuration.nTurns,
				turns = duration,
				sourceType = ENUM.BonusSource.spellEffect,
				sourceID = SPELL_KEY
			}, false)
			server:addUnitBonus(battle, unit, {
				type = "INVINCIBLE",
				val = 1,
				duration = ENUM.BonusDuration.nTurns,
				turns = duration,
				sourceType = ENUM.BonusSource.spellEffect,
				sourceID = SPELL_KEY
			}, false)
			server:addUnitBonus(battle, unit, {
				type = "NONE",
				val = radius(mechanics),
				duration = ENUM.BonusDuration.nTurns,
				turns = duration,
				sourceType = ENUM.BonusSource.spellEffect,
				sourceID = SPELL_KEY,
				hidden = true,
				description = "Time Stop stasis"
			}, false)
		end
	end
end

return Script
