local Base = require("spells/spellEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

-- Transfigure Matter is deliberately limited to ordinary physical scenery.
-- USUAL excludes siege walls/absolute obstacles, moats, and all spell-created
-- obstacles. Corpses and artifacts are units/objects rather than obstacles and
-- therefore cannot enter this list either.
local USUAL_OBSTACLE = ENUM.ObstacleType.usual
local MATTER_SHAPER_SKILL = "new-horizons:sorceryMagic"
local MATTER_SHAPER_PERK = "new-horizons:sorceryMagic.matterShaper"

local function contains(list, value)
	for _, existing in ipairs(list) do
		if existing == value then return true end
	end
	return false
end

local function validObstacle(obstacle)
	return obstacle ~= nil
		and obstacle:getObstacleType() == USUAL_OBSTACLE
		and obstacle:getHexes():size() > 0
end

local function targetObstacles(mechanics, target, all)
	local battle = mechanics:getBattle()
	local found = {}

	local function add(obstacle)
		if validObstacle(obstacle) and not contains(found, obstacle) then
			found[#found + 1] = obstacle
		end
	end

	if all then
		for _, obstacle in ipairs(battle:getAllObstacles()) do
			add(obstacle)
		end
	else
		for _, destination in ipairs(target) do
			if destination.hex ~= nil and destination.hex:isValid() then
				-- Querying getObstaclesOnPos would ask every obstacle for its
				-- affected tiles; moat instances intentionally have no affected
				-- footprint. Scan the authoritative list and inspect only ordinary
				-- obstacles, whose footprint is defined by the obstacle pack.
				for _, obstacle in ipairs(battle:getAllObstacles()) do
					if validObstacle(obstacle) and obstacle:getHexes():contains(destination.hex) then
						add(obstacle)
					end
				end
			end
		end
	end

	return found
end

local function placementHex(mechanics, obstacle, creature, side)
	-- The obstacle is intentionally still present while this applicability check
	-- runs. The authoritative apply path removes it before adding the golem, so
	-- neither accessibility nor occupancy can be queried as though the anchor
	-- were already empty. Ordinary scenery is placed only on valid, unit-free
	-- battlefield hexes; apply revalidates the obstacle immediately before the
	-- atomic authoritative removal/add sequence.
	local battle = mechanics:getBattle()
	local footprint = obstacle:getHexes()

	-- Lua obstacle proxies are shared-pointer wrappers and are not guaranteed to
	-- compare by the identity of the underlying C++ object. Exclude the selected
	-- obstacle by its stable anchor/category instead, so its own footprint does
	-- not make an otherwise valid target look occupied.
	local function isSelectedObstacle(other)
		return other:getPosition() == obstacle:getPosition()
			and other:getObstacleType() == obstacle:getObstacleType()
	end

	local function tileIsClear(hex)
		if not hex:isAvailable() then return false end
		if battle:getUnitByPos(hex, true) ~= nil then return false end

		for _, other in ipairs(battle:getAllObstacles()) do
			local otherType = other:getObstacleType()
			if otherType == ENUM.ObstacleType.moat then
				-- Moats do not expose an affected-tile footprint. Their anchor is
				-- still a forbidden placement location for a summoned unit.
				if other:getPosition() == hex then return false end
			elseif otherType == ENUM.ObstacleType.usual
				or otherType == ENUM.ObstacleType.absolute
				or otherType == ENUM.ObstacleType.spellCreated then
				if other:getHexes():contains(hex) and not isSelectedObstacle(other) then
					return false
				end
			end
		end

		-- Match the normal placement checks for terrain inside fortifications while
		-- still ignoring the selected scenery obstacle itself.
		if battle:hasFortifications() then
			local part = battle:hexToWallPart(hex)
			if part ~= ENUM.WallPart.invalid then
				if part == ENUM.WallPart.indestructiblePart
					or part == ENUM.WallPart.indestructibleGate
					or part == ENUM.WallPart.bottomTower
					or part == ENUM.WallPart.upperTower then
					return false
				end
				if battle:getWallState(part) ~= nil and battle:getWallState(part) > 0 then
					return false
				end
			end
		end
		return true
	end

	-- A Diamond Golem is double-wide. Its front hex is selected from the
	-- obstacle's real footprint (not its drawing anchor), and its rear hex must
	-- also become free when only this obstacle is removed.
	for index = 1, footprint:size() do
		local front = footprint:at(index)
		local rear = side == ENUM.BattleSide.attacker and front:copyToWest() or front:copyToEast()
		if tileIsClear(front) and (not creature:isDoubleWide() or tileIsClear(rear)) then
			return front
		end
	end

	return nil
end

function Script:hpPool(mechanics, obstacle)
	local hexes = obstacle:getHexes()
	local spellPower = mechanics:getEffectPower()
	local pool = 80 + 2 * spellPower + 50 * hexes:size()

	local hero = mechanics:getHeroCaster()
	if hero ~= nil and hero:hasActivePerk(MATTER_SHAPER_SKILL, MATTER_SHAPER_PERK) then
		-- Health is integral throughout the engine; use the same truncation as
		-- integer healing/damage calculations for the 25% perk bonus.
		pool = math.floor(pool * 5 / 4)
	end

	return pool
end

function Script:applicableGeneral(mechanics, problem)
	local creature = LIBRARY:getCreatureByName(self.id)
	local candidates = targetObstacles(mechanics, {}, true)
	for _, obstacle in ipairs(candidates) do
		if placementHex(mechanics, obstacle, creature, mechanics:getCasterSide()) ~= nil then
			return true
		end
	end

	problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
	return false
end

function Script:applicableTarget(mechanics, problem, target)
	local creature = LIBRARY:getCreatureByName(self.id)
	local candidates = targetObstacles(mechanics, target, false)
	for _, obstacle in ipairs(candidates) do
		if placementHex(mechanics, obstacle, creature, mechanics:getCasterSide()) ~= nil then
			return true
		end
	end

	problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
	return false
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	local result = {}
	local creature = LIBRARY:getCreatureByName(self.id)
	for _, obstacle in ipairs(targetObstacles(mechanics, spellTarget, false)) do
		-- Drawing anchors are not necessarily occupied (several stock obstacles
		-- exclude offset 0), so canonicalize to a deterministic real footprint hex.
		local position = placementHex(mechanics, obstacle, creature, mechanics:getCasterSide())
		if position ~= nil then
			result[#result + 1] = { hex = position }
		end
	end
	return result
end

function Script:adjustAffectedHexes(mechanics, hexes, spellTarget)
	for _, obstacle in ipairs(targetObstacles(mechanics, spellTarget, false)) do
		local footprint = obstacle:getHexes()
		for index = 1, footprint:size() do
			hexes:insert(footprint:at(index))
		end
	end
	return hexes
end

function Script:apply(mechanics, server, target)
	local creature = LIBRARY:getCreatureByName(self.id)
	local obstacles = targetObstacles(mechanics, target, false)
	local obstacle = obstacles[1]
	if obstacle == nil then
		return
	end
	local position = placementHex(mechanics, obstacle, creature, mechanics:getCasterSide())
	if position == nil then return end

	-- Compute before removal while the target's authoritative obstacle instance
	-- is unquestionably live; its shared proxy remains valid after the pack, but
	-- this ordering also keeps the operation easy to reason about atomically.
	local hpPool = self:hpPool(mechanics, obstacle)
	local maxHealth = creature:getMaxHealth()
	local count = math.ceil(hpPool / maxHealth)

	-- Both operations are authoritative server callbacks. The obstacle is
	-- removed first, then the newly-created temporary stack is wounded locally
	-- and committed through the normal UnitChanges update pack so clients and
	-- hypothetical AI see exactly the same aggregate health.
	local battle = mechanics:getBattle()
	server:removeObstacle(battle, obstacle)

	local unit = server:addUnit(battle, {
		count = count,
		type = creature,
		side = mechanics:getCasterSide(),
		position = position,
		summoned = true
	})
	if unit == nil then return end

	-- Army-wide health bonuses may increase the freshly-created stack above the
	-- creature template's base HP. Wound from the actual instantiated health so
	-- the canonical aggregate pool remains exact under artifacts and bonuses.
	local excess = unit:getAvailableHealth() - hpPool
	if excess > 0 then
		local state = unit:copy()
		state:damage(excess)
		server:changeUnit(battle, state)
	end
end

function Script:getHealthChange(mechanics, spellTarget)
	local creature = LIBRARY:getCreatureByName(self.id)
	local obstacle = targetObstacles(mechanics, spellTarget, false)[1]
	if obstacle == nil then
		return { hpDelta = 0, unitsDelta = 0 }
	end

	local hpPool = self:hpPool(mechanics, obstacle)
	return {
		hpDelta = hpPool,
		unitsDelta = math.ceil(hpPool / creature:getMaxHealth()),
		unitType = creature
	}
end

return Script
