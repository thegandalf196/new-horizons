local Base = require("combat/combatScript")
local BattleLog = require("battleLog")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local SPELL = "new-horizons:hexOfPain"

--- Sum the actual damage this attack dealt, excluding overkill against each target.
local function actualDamage(payload)
	local total = 0
	for _, target in ipairs(payload.targets or {}) do
		if target.unit then
			local dealt = math.max(0, target.damage or 0)
			local available = math.max(0, target.healthBeforeAttack or 0)
			total = total + math.min(dealt, available)
		end
	end
	return total
end

--- Spell.adjustDamage resolves the acting hero from the actor's side. Keep that context on the
--- side that originally cast Hex of Pain, even if the cursed stack attacks its own allies.
local function sourceOnCasterSide(battle, other, casterSide)
	if other and other:getSide() == casterSide then return other end

	local candidates = battle:getUnitsIf(function(unit)
		return unit:getSide() == casterSide
	end)
	return candidates[1]
end

function Script:onAfterAttack(server, battle, unit, other, payload)
	if not unit or not unit:isAlive() or not payload then return end

	local source = sourceOnCasterSide(battle, other, self.casterSide)
	if not source then return end

	local spell = LIBRARY:getSpellByName(SPELL)
	local inflicted = actualDamage(payload)
	local share = math.floor(inflicted * (self.damageSharePercent or 0) / 100)
	local rawDamage = (self.val or 0) + share
	local damage = spell:adjustDamage(battle, source, unit, rawDamage)
	if damage <= 0 then return end

	-- damageUnit emits an authoritative injury pack and does not start another attack event.
	local dealt, killed = server:damageUnit(battle, unit, damage, false, source)
	BattleLog.spellDamage(server, battle, spell, unit, dealt, killed)
end

return Script
