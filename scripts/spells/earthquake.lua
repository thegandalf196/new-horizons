local Damage = require("spells/damage")
local Catapult = require("spells/catapult")
local Script = setmetatable({}, {__index = Damage})
Script.__index = Script

local NATURE_MAGIC_SKILL = "new-horizons:natureMagic"
local GEOMANCER_PERK = "new-horizons:natureMagic.geomancer"

local DIRECTIONS = {
	"copyToNorthWest", "copyToNorthEast", "copyToEast",
	"copyToSouthEast", "copyToSouthWest", "copyToWest"
}

-- Catapult's helper methods are intentionally shared so a saved legacy ruleset
-- keeps its original hit-chance, random target selection, and hit-quality path.
Script.hitChance = Catapult.hitChance
Script.randomDamage = Catapult.randomDamage
Script.potentialTargets = Catapult.potentialTargets

-- EWallPart numeric order, used as the stable tie-break for equidistant siege
-- sections. Keep this list explicit rather than relying on table iteration.
local SECTIONS_BY_PART_ID = {
	ENUM.WallPart.keep,
	ENUM.WallPart.bottomTower,
	ENUM.WallPart.bottomWall,
	ENUM.WallPart.belowGate,
	ENUM.WallPart.overGate,
	ENUM.WallPart.upperWall,
	ENUM.WallPart.upperTower,
	ENUM.WallPart.gate
}

local function isCanonical(mechanics)
	return mechanics:usesNewHorizonsEarthquake()
end

local function hexKey(hex)
	return tostring(hex:getX()) .. ":" .. tostring(hex:getY())
end

local function selectedHex(target)
	if target == nil then return nil end
	for _, destination in ipairs(target) do
		local hex = destination.hex
		if hex ~= nil and hex:isValid() then
			return hex
		end
	end
	return nil
end

local function area(center, radius)
	if center == nil or not center:isValid() then return {}, {} end

	local result = {}
	local seen = {[hexKey(center)] = true}
	local queue = {{hex = center, distance = 0}}
	local cursor = 1
	while cursor <= #queue do
		local entry = queue[cursor]
		cursor = cursor + 1
		if entry.hex:isValid() then
			result[#result + 1] = entry.hex
		end
		if entry.distance < radius then
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
	return result, seen
end

local function footprintIntersects(unit, affected)
	local footprint = unit:getHexes()
	for index = 1, footprint:size() do
		if affected[hexKey(footprint:at(index))] then
			return true
		end
	end
	return false
end

local function geomancerActive(mechanics)
	local hero = mechanics:getHeroCaster()
	return hero ~= nil and hero:hasActivePerk(NATURE_MAGIC_SKILL, GEOMANCER_PERK)
end

local function centerFrom(target)
	-- The engine preserves the selected LOCATION anchor in spellTarget for this
	-- spell, including its range-X configuration.
	return selectedHex(target)
end

local function selectedSiegePart(battle, center)
	if center == nil or not battle:hasFortifications() then return nil end
	local part = battle:hexToWallPart(center)
	if part == ENUM.WallPart.invalid or not battle:isWallPartAttackable(part) then
		return nil
	end
	return part
end

local function hexDistances(center)
	local distances = {[hexKey(center)] = 0}
	local queue = {center}
	local cursor = 1
	while cursor <= #queue do
		local hex = queue[cursor]
		cursor = cursor + 1
		local distance = distances[hexKey(hex)]
		for _, direction in ipairs(DIRECTIONS) do
			local nextHex = hex[direction](hex)
			local key = hexKey(nextHex)
			if nextHex:isValid() and distances[key] == nil then
				distances[key] = distance + 1
				queue[#queue + 1] = nextHex
			end
		end
	end
	return distances
end

local function plannedSections(mechanics, center)
	local battle = mechanics:getBattle()
	local selected = selectedSiegePart(battle, center)
	if selected == nil then return {} end

	local selectedHexForPart = battle:wallPartToBattleHex(selected)
	if not selectedHexForPart:isValid() then return {} end
	local distances = hexDistances(selectedHexForPart)
	local candidates = {}
	for partId, part in ipairs(SECTIONS_BY_PART_ID) do
		if battle:isWallPartAttackable(part) then
			local partHex = battle:wallPartToBattleHex(part)
			if partHex:isValid() then
				candidates[#candidates + 1] = {
					part = part,
					partId = partId - 1,
					distance = distances[hexKey(partHex)] or math.huge
				}
			end
		end
	end

	table.sort(candidates, function(left, right)
		if left.distance ~= right.distance then
			return left.distance < right.distance
		end
		return left.partId < right.partId
	end)

	local count = math.max(1, mechanics:getNewHorizonsEarthquakeSectionCount())
	local result = {selected}
	local seen = {[selected] = true}
	for _, candidate in ipairs(candidates) do
		if #result >= count then break end
		if not seen[candidate.part] then
			seen[candidate.part] = true
			result[#result + 1] = candidate.part
		end
	end
	return result
end

local function canonicalFieldTargets(self, mechanics, center)
	local battle = mechanics:getBattle()
	local affectedHexes, affectedSet = area(center, mechanics:getNewHorizonsEarthquakeParameter("radius"))
	local result = {{hex = center}}
	local units = battle:getUnitsIf(function(unit)
		return unit:isAlive()
			and unit:isValidTarget(false)
			and not unit:hasBonuses({type = "FLYING"})
			and Damage.isReceptive(self, mechanics, unit)
			and footprintIntersects(unit, affectedSet)
	end)
	for _, unit in ipairs(units) do
		result[#result + 1] = {unit = unit, hex = unit:getPosition()}
	end
	return result, affectedHexes
end

function Script:adjustTargetTypes(mechanics, types)
	if isCanonical(mechanics) then
		return {ENUM.AimType.location}
	end
	return Catapult.adjustTargetTypes(self, mechanics, types)
end

function Script:applicableGeneral(mechanics, problem)
	if not isCanonical(mechanics) then
		return Catapult.applicableGeneral(self, mechanics, problem)
	end

	local battle = mechanics:getBattle()
	if battle:hasFortifications() then
		for _, part in ipairs(SECTIONS_BY_PART_ID) do
			if battle:isWallPartAttackable(part) then return true end
		end
		problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
		return false
	end
	return true
end

function Script:applicableTarget(mechanics, problem, target)
	if not isCanonical(mechanics) then
		return Catapult.applicableTarget(self, mechanics, problem, target)
	end

	local center = selectedHex(target)
	if center == nil or not center:isAvailable() then
		problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
		return false
	end
	local battle = mechanics:getBattle()
	if battle:hasFortifications() and #plannedSections(mechanics, center) == 0 then
		problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
		return false
	end
	return true
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	if not isCanonical(mechanics) then
		return Catapult.transformTarget(self, mechanics, aimPoint, spellTarget)
	end

	local center = selectedHex(aimPoint)
	if center == nil then center = selectedHex(spellTarget) end
	if center == nil then return {} end

	if mechanics:getBattle():hasFortifications() then
		return {{hex = center}}
	end
	local targets = canonicalFieldTargets(self, mechanics, center)
	return targets
end

function Script:filterTarget(mechanics, target)
	if not isCanonical(mechanics) then
		return Catapult.filterTarget(self, mechanics, target)
	end

	local result = {}
	local preservedAnchor = false
	for _, destination in ipairs(target) do
		if destination.unit == nil then
			if not preservedAnchor and destination.hex ~= nil and destination.hex:isValid() then
				result[#result + 1] = {hex = destination.hex}
				preservedAnchor = true
			end
		elseif destination.unit:isAlive()
			and destination.unit:isValidTarget(false)
			and not destination.unit:hasBonuses({type = "FLYING"}) then
			for _, filtered in ipairs(Damage.filterTarget(self, mechanics, {destination})) do
				result[#result + 1] = filtered
			end
		end
	end
	return result
end

function Script:adjustAffectedHexes(mechanics, hexes, spellTarget)
	if not isCanonical(mechanics) then
		return Catapult.adjustAffectedHexes(self, mechanics, hexes, spellTarget)
	end

	local center = centerFrom(spellTarget)
	if center == nil then return hexes end
	local battle = mechanics:getBattle()
	if battle:hasFortifications() then
		for _, part in ipairs(plannedSections(mechanics, center)) do
			local sectionHex = battle:wallPartToBattleHex(part)
			if sectionHex:isValid() then hexes:insert(sectionHex) end
		end
		return hexes
	end

	local affected = area(center, mechanics:getNewHorizonsEarthquakeParameter("radius"))
	for _, hex in ipairs(affected) do
		if hex:isAvailable() then hexes:insert(hex) end
	end
	return hexes
end

local function createFracturedGround(self, mechanics, server, affectedHexes)
	local battle = mechanics:getBattle()
	local turns = mechanics:getNewHorizonsEarthquakeParameter("duration")
	if geomancerActive(mechanics) then turns = turns + 1 end
	turns = mechanics:adjustEffectDuration(turns)
	local movementCost = mechanics:getNewHorizonsEarthquakeParameter("movementCost")
	local animation = self.animation or "C17SPE1"

	for _, hex in ipairs(affectedHexes) do
		if hex:isAvailable() then
			server:addObstacle(battle, {
				pos = hex,
				obstacleType = ENUM.ObstacleType.spellCreated,
				spell = mechanics:getSpell(),
				turnsRemaining = turns,
				casterSpellPower = mechanics:getEffectPower(),
				casterPowerDivisor = mechanics:getEffectPowerDivisor(),
				spellLevel = mechanics:getEffectLevel(),
				casterSide = mechanics:getCasterSide(),
				movementCost = movementCost,
				passable = true,
				trap = false,
				removeOnTrigger = false,
				trigger = "",
				hidden = false,
				nativeVisible = true,
				appearSound = "",
				appearAnimation = "",
				animation = animation,
				removalAnimation = "",
				customSize = {hex}
			})
		end
	end
end

function Script:apply(mechanics, server, target)
	if not isCanonical(mechanics) then
		return Catapult.apply(self, mechanics, server, target)
	end

	local center = selectedHex(target)
	if center == nil then return end
	local battle = mechanics:getBattle()
	if battle:hasFortifications() then
		local structuralDamage = mechanics:getNewHorizonsEarthquakeParameter("structuralDamage")
		if geomancerActive(mechanics) then
			structuralDamage = math.floor(structuralDamage * 125 / 100)
		end
		for _, part in ipairs(plannedSections(mechanics, center)) do
			server:damageFortification(battle, part, structuralDamage)
		end
		return
	end

	local affectedHexes = area(center, mechanics:getNewHorizonsEarthquakeParameter("radius"))
	-- Shared damage resolution preserves ordinary combat-log feedback as well
	-- as actual HP loss. The retained location anchor has no unit and is ignored.
	Damage.apply(self, mechanics, server, target)

	createFracturedGround(self, mechanics, server, affectedHexes)
end

function Script:getHealthChange(mechanics, spellTarget)
	if not isCanonical(mechanics) then
		return Catapult.getHealthChange(self, mechanics, spellTarget)
	end

	local units = {}
	for _, destination in ipairs(spellTarget) do
		local unit = destination.unit
		if unit ~= nil and unit:isAlive()
			and unit:isValidTarget(false) and not unit:hasBonuses({type = "FLYING"}) then
			units[#units + 1] = destination
		end
	end
	return Damage.getHealthChange(self, mechanics, units)
end

return Script
