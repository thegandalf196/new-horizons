local Damage = require("spells/damage")
local Script = setmetatable({}, {__index = Damage})
Script.__index = Script

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
	return mechanics:usesNewHorizonsHavocStructures()
end

local function isMeteorShower(mechanics)
	local spell = mechanics:getSpell()
	return spell and spell:getJsonKey() == "core:meteorShower"
end

local function isArmageddon(mechanics)
	local spell = mechanics:getSpell()
	return spell and spell:getJsonKey() == "core:armageddon"
end

local function hexKey(hex)
	return tostring(hex:getX()) .. ":" .. tostring(hex:getY())
end

local function affectedHexSet(target)
	local result = {}
	for _, dest in ipairs(target or {}) do
		-- Unit destinations are for creature effects only. In particular, a
		-- double-wide unit's primary position must not widen Meteor's scenery
		-- footprint, and filtering a unit must not erase its impacted hex.
		if not dest.unit and dest.hex and dest.hex:isValid() then
			result[hexKey(dest.hex)] = true
		end
	end
	return result
end

local function obstacleIntersects(obstacle, affected)
	local footprint = obstacle:getHexes()
	for index = 1, footprint:size() do
		if affected[hexKey(footprint:at(index))] then
			return true
		end
	end
	return false
end

local function ordinaryObstacleTargets(battle, affected, allBattlefield)
	local result = {}
	for _, obstacle in ipairs(battle:getAllObstacles()) do
		if obstacle:getObstacleType() == ENUM.ObstacleType.usual
			and (allBattlefield or obstacleIntersects(obstacle, affected)) then
			table.insert(result, obstacle)
		end
	end
	return result
end

local function fortificationTargets(mechanics, affected, allBattlefield)
	local battle = mechanics:getBattle()
	if not battle:hasFortifications() or mechanics:getNewHorizonsHavocStructuralDamage() <= 0 then
		return {}
	end

	local result = {}
	for _, part in ipairs(SECTIONS_BY_PART_ID) do
		if battle:isWallPartAttackable(part) and battle:getWallStructuralHP(part) > 0 then
			local hex = battle:wallPartToBattleHex(part)
			if hex:isValid() and (allBattlefield or affected[hexKey(hex)]) then
				table.insert(result, part)
			end
		end
	end
	return result
end

local function structuralTargets(mechanics, target, allBattlefield)
	local battle = mechanics:getBattle()
	local affected = affectedHexSet(target)
	return ordinaryObstacleTargets(battle, affected, allBattlefield),
		fortificationTargets(mechanics, affected, allBattlefield)
end

function Script:applicableGeneral(mechanics, problem)
	if not isCanonical(mechanics) then
		return Damage.applicableGeneral(self, mechanics, problem)
	end

	local obstacles, fortifications = structuralTargets(mechanics, {}, true)
	if #obstacles > 0 or #fortifications > 0 then
		return true
	end
	return Damage.applicableGeneral(self, mechanics, problem)
end

function Script:applicableTarget(mechanics, problem, target)
	if not isCanonical(mechanics) then
		return Damage.applicableTarget(self, mechanics, problem, target)
	end
	if Damage.applicableTarget(self, mechanics, problem, target) then
		return true
	end

	local allBattlefield = isArmageddon(mechanics)
	local obstacles, fortifications = structuralTargets(mechanics, target, allBattlefield)
	return #obstacles > 0 or #fortifications > 0
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	local result = Damage.transformTarget(self, mechanics, aimPoint, spellTarget)
	if not isCanonical(mechanics) or not isMeteorShower(mechanics) then
		return result
	end

	-- Damage transforms a location area into units. Keep its full original hex
	-- footprint as metadata so scenery and wall sections remain targetable even
	-- when every impacted hex is empty or every creature rejects the spell.
	local seen = affectedHexSet(result)
	local function addHex(destination)
		local hex = destination and destination.hex
		if destination and not destination.unit and hex and hex:isValid() and not seen[hexKey(hex)] then
			seen[hexKey(hex)] = true
			table.insert(result, {hex = hex})
		end
	end
	for _, destination in ipairs(aimPoint or {}) do addHex(destination) end
	for _, destination in ipairs(spellTarget or {}) do addHex(destination) end
	return result
end

function Script:filterTarget(mechanics, target)
	if not isCanonical(mechanics) then
		return Damage.filterTarget(self, mechanics, target)
	end

	local result = {}
	for _, destination in ipairs(target) do
		local unit = destination.unit
		if unit then
			if self:isValidTarget(mechanics, unit) and self:isReceptive(mechanics, unit) then
				table.insert(result, destination)
			end
		elseif destination.hex and destination.hex:isValid() then
			table.insert(result, destination)
		end
	end
	return result
end

function Script:apply(mechanics, server, target)
	if not isCanonical(mechanics) then
		return Damage.apply(self, mechanics, server, target)
	end

	Damage.apply(self, mechanics, server, target)

	local battle = mechanics:getBattle()
	local allBattlefield = isArmageddon(mechanics)
	local obstacles, fortifications = structuralTargets(mechanics, target, allBattlefield)
	local removedObstacles = 0
	for _, obstacle in ipairs(obstacles) do
		server:removeObstacle(battle, obstacle)
		removedObstacles = removedObstacles + 1
	end

	local damagedSections, totalStructuralDamage = 0, 0
	local structuralDamage = mechanics:getNewHorizonsHavocStructuralDamage()
	for _, part in ipairs(fortifications) do
		local before = battle:getWallStructuralHP(part)
		server:damageFortification(battle, part, structuralDamage)
		local after = battle:getWallStructuralHP(part)
		local actualDamage = math.max(0, before - after)
		if actualDamage > 0 then
			damagedSections = damagedSections + 1
			totalStructuralDamage = totalStructuralDamage + actualDamage
		end
	end

	if server:describeChanges() and (removedObstacles > 0 or totalStructuralDamage > 0) then
		local spellName = allBattlefield and "Armageddon" or "Meteor Shower"
		server:appendLog(battle, {
			appendRaw = {string.format(
				"%s destroyed %d ordinary battlefield obstacle(s) and dealt %d structural damage across %d fortification section(s).",
				spellName, removedObstacles, totalStructuralDamage, damagedSections)}
		})
	end
end

return Script
