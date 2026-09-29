local Base = require("combat/combatScript")
local BattleLog = require("battleLog")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local SPELL = "new-horizons:shadowGift"
local BASIS_POINTS_PER_WHOLE = 10000

--- Damage actually inflicted on one stack, excluding overkill.
local function actualDamage(target)
	if not target or not target.unit then return 0 end
	local dealt = math.max(0, target.damage or 0)
	local available = math.max(0, target.healthBeforeAttack or 0)
	return math.min(dealt, available)
end

--- Preserve the original caster-side spell context even if the bearer attacks an ally.
local function sourceOnCasterSide(battle, bearer, other, casterSide)
	if bearer and bearer:getSide() == casterSide then return bearer end
	if other and other:getSide() == casterSide then return other end
	local candidates = battle:getUnitsIf(function(unit)
		return unit:getSide() == casterSide
	end)
	return candidates[1]
end

function Script:onAfterAttack(server, battle, unit, other, payload)
	if not unit or not unit:isAlive() or not payload then return end
	local source = sourceOnCasterSide(battle, unit, other, self.casterSide)
	if not source then return end

	local spell = LIBRARY:getSpellByName(SPELL)
	for _, target in ipairs(payload.targets or {}) do
		local victim = target.unit
		local inflicted = actualDamage(target)
		if victim and victim ~= unit and inflicted > 0 then
			local rawBonusDamage = math.floor(inflicted * (self.val or 0) / BASIS_POINTS_PER_WHOLE)
			if rawBonusDamage > 0 then
				local damage = spell:adjustDamage(battle, source, victim, rawBonusDamage)
				if damage > 0 then
					-- Partition the bonus per struck stack: cleave and other multi-target
					-- attacks receive a separate Shadow packet for each target's actual loss.
					-- It is a packet, not another attack, so this event cannot recurse.
					local dealt, killed = server:damageUnitAsSpell(battle, victim, damage, spell, source)
					BattleLog.spellDamage(server, battle, spell, victim, dealt, killed)
				end
			end
		end
	end
end

return Script
