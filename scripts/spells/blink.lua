local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local function selectedUnit(mechanics, target)
	if not target or #target ~= 1 then return nil end
	local destination = target[1]
	if destination.unit then return destination.unit end
	if destination.hex and destination.hex:isValid() then
		return mechanics:getBattle():getUnitByPos(destination.hex, true)
	end
	return nil
end

local function hasLegalTarget(mechanics, unit)
	if not unit or not unit:isValidTarget(false) then return false end
	if mechanics:isSmart() and not mechanics:ownerMatches(unit) then return false end
	if not Script.isReceptive(Base, mechanics, unit) then return false end
	local preview = mechanics:getBlinkPreview(unit)
	return preview ~= nil and #preview.legalDestinations > 0
end

function Script:applicableGeneral(mechanics, problem)
	if not mechanics:usesNewHorizonsMagicV3() then
		problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
		return false
	end

	local targets = mechanics:getBattle():getUnitsIf(function(unit)
		return hasLegalTarget(mechanics, unit)
	end)
	if #targets == 0 then
		problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget)
		return false
	end
	return true
end

function Script:applicableTarget(mechanics, problem, target)
	local unit = selectedUnit(mechanics, target)
	if hasLegalTarget(mechanics, unit) then return true end
	problem:addStandard(mechanics, ENUM.SpellCastProblem.wrongSpellTarget)
	return false
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	local transformed = Base.transformTarget(self, mechanics, aimPoint, spellTarget)
	if #transformed > 0 then return transformed end

	-- Creature selection normally supplies a Unit destination. Keep the raw-hex
	-- contract safe too, resolving its occupant before charging or applying.
	local unit = selectedUnit(mechanics, aimPoint)
	if not unit or not hasLegalTarget(mechanics, unit) then return {} end
	return {{unit = unit, hex = unit:getPosition()}}
end

function Script:apply(mechanics, server, target)
	local unit = selectedUnit(mechanics, target)
	local preview = unit and mechanics:getBlinkPreview(unit)
	if not preview or #preview.legalDestinations == 0 then return end

	local destinations = preview.legalDestinations
	local first = destinations[server:rngInt(1, #destinations)]
	local destination = first
	if preview.blinkmaster then
		-- Independent uniform draws with replacement, as specified. The shared
		-- mechanics helper applies engine hex-distance and stable tie-breaking.
		local second = destinations[server:rngInt(1, #destinations)]
		destination = mechanics:chooseBlinkmasterDestination(unit, first, second)
		if not destination:isValid() then return end
	end

	server:moveUnit(mechanics:getBattle(), unit, destination, true)
end

return Script
