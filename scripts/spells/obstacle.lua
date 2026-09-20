local Base = require("spells/spellEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local DIRECTION_METHOD =
{
	TL = "copyToNorthWest",
	TR = "copyToNorthEast",
	R  = "copyToEast",
	BR = "copyToSouthEast",
	BL = "copyToSouthWest",
	L  = "copyToWest",
}

local function applyShape(hex, shape)
	local result = hex
	if shape == nil then return result end
	for _, dir in ipairs(shape) do
		local method = DIRECTION_METHOD[dir]
		if method then
			result = result[method](result)
		end
	end
	return result
end

local function sideOptions(self, side)
	if side == ENUM.BattleSide.attacker then
		return self.attacker or {}
	end
	return self.defender or {}
end

local function otherSide(side)
	if side == ENUM.BattleSide.attacker then return ENUM.BattleSide.defender end
	return ENUM.BattleSide.attacker
end

-- New Horizons Land Mine is the one obstacle whose placement is a player
-- supplied vector rather than a random patch.  Keep this predicate scoped to
-- the saved ruleset and canonical spell identity so legacy Land Mine and all
-- other obstacle spells retain their existing behavior.
local function isNewHorizonsLandMine(mechanics)
	return mechanics:usesNewHorizonsMagic()
		and mechanics:getSpell():getJsonKey() == "core:landMine"
end

local function newHorizonsLandMineCount(mechanics)
	local power = mechanics:getEffectPower()
	if power < 100 then return 2 end
	if power < 200 then return 3 end
	return 4
end

local function shapesFor(opts)
	local shapes = opts.shape
	if shapes == nil or #shapes == 0 then return { {} } end
	return shapes
end

local function rangesFor(opts)
	local ranges = opts.range
	if ranges == nil or #ranges == 0 then return { {} } end
	return ranges
end

local function isHexAvailable(battle, hex, mustBeClear)
	if not hex:isAvailable() then return false end
	if not mustBeClear then return true end

	if battle:getUnitByPos(hex, true) ~= nil then return false end

	for _, o in ipairs(battle:getObstaclesOnPos(hex, false)) do
		if o:getObstacleType() ~= ENUM.ObstacleType.moat then
			return false
		end
	end

	if battle:hasFortifications() then
		local part = battle:hexToWallPart(hex)
		if part == ENUM.WallPart.invalid then return true end
		if part == ENUM.WallPart.indestructiblePart or part == ENUM.WallPart.indestructibleGate then return false end
		if part == ENUM.WallPart.bottomTower or part == ENUM.WallPart.upperTower then return false end
		if battle:getWallState(part) ~= nil and battle:getWallState(part) > 0 then return false end
	end
	return true
end

function Script:applicableGeneral(mechanics, problem)
	if self.hidden and self.hideNative and not isNewHorizonsLandMine(mechanics) then
		local battle = mechanics:getBattle()
		if battle:hasNativeStack(otherSide(mechanics:getCasterSide())) then
			problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
			return false
		end
	end
	return true
end

local function noRoomToPlace(mechanics, problem)
	problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
	return false
end

function Script:applicableTarget(mechanics, problem, target)
	if isNewHorizonsLandMine(mechanics) then
		local required = newHorizonsLandMineCount(mechanics)
		if #target ~= required then return noRoomToPlace(mechanics, problem) end

		local battle = mechanics:getBattle()
		local seen = {}
		for _, dest in ipairs(target) do
			local hex = dest.hex
			if dest.unit ~= nil or not hex:isAvailable() then
				return noRoomToPlace(mechanics, problem)
			end
			local key = hex:getY() * 17 + hex:getX()
			if seen[key] or not isHexAvailable(battle, hex, true) then
				return noRoomToPlace(mechanics, problem)
			end
			seen[key] = true
		end
		return true
	end

	if mechanics:isMassive() then return true end

	if #target == 0 then return noRoomToPlace(mechanics, problem) end

	local battle = mechanics:getBattle()
	local opts   = sideOptions(self, mechanics:getCasterSide())
	local shapes = shapesFor(opts)

	for _, dest in ipairs(target) do
		for _, shape in ipairs(shapes) do
			local hex = applyShape(dest.hex, shape)
			if not isHexAvailable(battle, hex, true) then
				return noRoomToPlace(mechanics, problem)
			end
		end
	end
	return true
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	if isNewHorizonsLandMine(mechanics) then
		-- BattleSpellMechanics deliberately leaves a massive spell's normal
		-- transformed target empty.  Land Mine opts into the original action
		-- vector here so every selected hex reaches validation and apply().
		return aimPoint
	end
	if mechanics:isMassive() then return {} end

	local opts   = sideOptions(self, mechanics:getCasterSide())
	local ranges = rangesFor(opts)

	local result = {}
	for _, dest in ipairs(spellTarget) do
		for _, range in ipairs(ranges) do
			local hex = applyShape(dest.hex, range)
			result[#result+1] = { hex = hex }
		end
	end
	return result
end

function Script:adjustAffectedHexes(mechanics, hexes, spellTarget)
	local transformed = self:transformTarget(mechanics, spellTarget, spellTarget)
	local opts   = sideOptions(self, mechanics:getCasterSide())
	local shapes = shapesFor(opts)
	for _, dest in ipairs(transformed) do
		for _, shape in ipairs(shapes) do
			local hex = applyShape(dest.hex, shape)
			if hex:isValid() then
				hexes:insert(hex)
			end
		end
	end
	return hexes
end

local function buildDescriptor(self, mechanics, side, hex, customSize)
	local spell = mechanics:getSpell()
	local opts  = sideOptions(self, side)
	local newMine = isNewHorizonsLandMine(mechanics)
	return {
		pos              = hex,
		obstacleType     = ENUM.ObstacleType.spellCreated,
		spell            = spell,
		casterSpellPower = mechanics:getEffectPower(),
		casterPowerDivisor = mechanics:getEffectPowerDivisor(),
		spellLevel       = mechanics:getEffectLevel(),
		casterSide       = side,
		-- Land Mine's direct-damage value is evaluated by the authoritative
		-- cast mechanics and retained in the generic obstacle damage floor.  The
		-- trigger proxy recognizes this canonical source and uses it exactly.
		minimalDamage    = newMine and mechanics:getEffectValue() or (self.minimalDamage or 0),
		damageSnapshot   = newMine,
		turnsRemaining   = self.turnsRemaining or -1,
		hidden           = self.hidden or false,
		passable         = self.passable or false,
		-- Avoid Lua's `a and false or b` pitfall: when `newMine` is true the
		-- intermediate false would select `b` and accidentally reveal mines to
		-- native enemy armies.
		nativeVisible    = not (newMine or self.hideNative or false),
		trap             = self.trap or false,
		removeOnTrigger  = self.removeOnTrigger or false,
		trigger          = self.triggerAbility or "",
		appearSound      = opts.appearSound or "",
		appearAnimation  = opts.appearAnimation or "",
		animation        = opts.animation or "",
		removalAnimation = opts.removalAnimation or "",
		customSize       = customSize,
	}
end

local function collectAvailable(battle, hexes)
	local out = {}
	for _, hex in ipairs(hexes) do
		if isHexAvailable(battle, hex, true) then
			out[#out+1] = hex
		end
	end
	return out
end

local function hexesFromArray(arr)
	local list = {}
	for i = 1, arr:size() do
		list[#list+1] = arr:at(i)
	end
	return list
end

function Script:apply(mechanics, server, target)
	local battle    = mechanics:getBattle()
	local side      = mechanics:getCasterSide()
	local opts      = sideOptions(self, side)
	local shapes    = shapesFor(opts)
	local patchCount = self.patchCount or 0
	local newMine = isNewHorizonsLandMine(mechanics)

	local destinations = {}

	if newMine then
		-- The C++ action processor and this effect's applicability both enforce
		-- exact count/unique/empty semantics.  Never randomize or silently place
		-- fewer mines for an invalid NH action.
		if #target ~= newHorizonsLandMineCount(mechanics) then return end
		for _, dest in ipairs(target) do
			destinations[#destinations+1] = dest.hex
		end
	elseif patchCount > 0 then
		local candidates
		if mechanics:isMassive() then
			candidates = hexesFromArray(battle:getAllPossibleHexes())
		else
			candidates = {}
			for _, dest in ipairs(target) do
				candidates[#candidates+1] = dest.hex
			end
		end

		local available = collectAvailable(battle, candidates)

		for i = #available, 2, -1 do
			local j = server:rngInt(1, i)
			available[i], available[j] = available[j], available[i]
		end

		local toPlace = math.min(patchCount, #available)
		for i = 1, toPlace do
			destinations[#destinations+1] = available[i]
		end
	else
		for _, dest in ipairs(target) do
			destinations[#destinations+1] = dest.hex
		end
	end

	for _, hex in ipairs(destinations) do
		local customSize = {}
		for _, shape in ipairs(shapes) do
			local shaped = applyShape(hex, shape)
			customSize[#customSize+1] = shaped
		end
		local descriptor = buildDescriptor(self, mechanics, side, hex, customSize)
		server:addObstacle(battle, descriptor)
	end
end

return Script
