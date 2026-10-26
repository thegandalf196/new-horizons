-- Focused execution of the production Lua effect with callback/server doubles.
local source = debug.getinfo(1, "S").source:sub(2)
local root = source:match("^(.*)/tools/tests/fixtures/")
package.path = root .. "/scripts/?.lua;" .. package.path
ENUM = {BonusSource = {spellEffect = "SPELL_EFFECT"}, BonusDuration = {nTurns = "N_TURNS"}}
local Script = require("spells/theChosen")
local key = "new-horizons:theChosen"
local function list(values)
	return {values = values, size = function(self) return #self.values end,
		filter = function(self, predicate)
			local result = {}
			for _, bonus in ipairs(self.values) do
				if predicate(bonus) then result[#result + 1] = bonus end
			end
			return list(result)
		end}
end
local function marker(identity)
	return {getSourceID = function() return identity end}
end
local function unit(side, living, mechanical)
	return {side = side, living = living, mechanical = mechanical, alive = true,
		bonuses = {}, isValidTarget = function(self) return self.alive end,
		isLiving = function(self) return self.living end,
		isInvincible = function() return false end,
		isTimeStopped = function(self) return self.stopped or false end,
		hasBonuses = function(self) return self.mechanical end,
		getSide = function(self) return self.side end,
		getBonuses = function(self) return list(self.bonuses) end}
end
local first = unit(0, true, false)
local nextTarget = unit(0, true, false)
local corpse = unit(0, true, false)
corpse.alive = false
local controlled = unit(0, true, false)
local enemy = unit(1, true, false)
first.bonuses = {marker(key), marker(key), marker("other")}
corpse.bonuses = {marker(key)}
controlled.bonuses = {marker(key)}
enemy.bonuses = {marker(key)}
local units = {first, nextTarget, corpse, controlled, enemy}
local battle = {getUnitsIf = function(self, predicate)
	local result = {}
	for _, candidate in ipairs(units) do
		if predicate(candidate) then result[#result + 1] = candidate end
	end
	return result
end}
local mechanics = {nominal = 4, extension = 0,
	ownerMatches = function(self, target) return target ~= controlled and target.side == 0 end,
	isReceptive = function(self, target) return not target.immune end,
	getBattle = function() return battle end,
	getTheChosenDuration = function(self) return self.nominal + self.extension end,
	getTheChosenJoustingPercent = function() return 5 end,
	adjustEffectDuration = function(self, base) return base + self.extension end}
local events = {}
local server = {
	removeUnitBonuses = function(self, _, target, bonuses)
		events[#events + 1] = "remove"
		local keep = {}
		for _, bonus in ipairs(target.bonuses) do
			local remove = false
			for _, deleted in ipairs(bonuses.values) do if deleted == bonus then remove = true end end
			if not remove then keep[#keep + 1] = bonus end
		end
		target.bonuses = keep
	end,
	addUnitBonus = function(self, _, target, bonus, cumulative)
		events[#events + 1] = "add"
		assert(not cumulative)
		assert(bonus.valueType == "INDEPENDENT_MAX")
		assert(bonus.duration == "N_TURNS" and bonus.turns == mechanics.nominal + mechanics.extension)
		assert(bonus.sourceType == "SPELL_EFFECT" and bonus.sourceID == key)
		bonus.getSourceID = function() return bonus.sourceID end
		target.bonuses[#target.bonuses + 1] = bonus
	end}
-- Immune, dead, mechanical, nonliving, hostile and resisted(empty) destinations
-- must preserve every existing source sibling without issuing mutation calls.
nextTarget.immune = true
Script:apply(mechanics, server, {{unit = nextTarget}})
nextTarget.immune = false
for _, invalid in ipairs({corpse, unit(0, false, false), unit(0, true, true), enemy}) do
	Script:apply(mechanics, server, {{unit = invalid}})
end
Script:apply(mechanics, server, {})
assert(#events == 0 and #first.bonuses == 3)
-- Old stopped recipients block targeting and defensive application atomically.
first.stopped = true
assert(not Script:isValidTarget(mechanics, nextTarget))
Script:apply(mechanics, server, {{unit = nextTarget}})
assert(#events == 0 and #first.bonuses == 3)
first.stopped = false
nextTarget.stopped = true
assert(not Script:isValidTarget(mechanics, nextTarget))
Script:apply(mechanics, server, {{unit = nextTarget}})
assert(#events == 0 and #first.bonuses == 3)
nextTarget.stopped = false
assert(Script:isValidTarget(mechanics, nextTarget))
Script:apply(mechanics, server, {{unit = nextTarget}})
assert(table.concat(events, ",") == "remove,remove,remove,add,add,add,add")
assert(#first.bonuses == 1 and #corpse.bonuses == 0 and #controlled.bonuses == 0)
assert(#enemy.bonuses == 1 and #nextTarget.bonuses == 4)
local expected = {ADDITIONAL_ATTACK = 1, UNLIMITED_RETALIATIONS = 1, FLYING = 1, JOUSTING = 5}
for _, bonus in ipairs(nextTarget.bonuses) do assert(expected[bonus.type] == bonus.val) end
Script:apply(mechanics, server, {{unit = nextTarget}})
assert(#nextTarget.bonuses == 4)

-- The accepted duration helper returns extensions after the nominal cap;
-- installing the effect must preserve those extra rounds without re-capping.
mechanics.extension = 2
Script:apply(mechanics, server, {{unit = nextTarget}})
assert(#nextTarget.bonuses == 4)
for _, bonus in ipairs(nextTarget.bonuses) do assert(bonus.turns == 6) end
