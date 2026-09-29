local Base = require("combat/combatScript")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local BASIS_POINTS_PER_WHOLE = 10000

--- Sum damage actually removed from every victim, excluding overkill per stack.
local function actualDamage(payload)
	local total = 0
	for _, target in ipairs(payload.targets or {}) do
		if target.unit then
			local dealt = math.max(0, target.damage or 0)
			if target.healthBeforeAttack ~= nil then
				dealt = math.min(dealt, math.max(0, target.healthBeforeAttack))
			end
			total = total + dealt
		end
	end
	return total
end

function Script:onAfterAttack(server, battle, unit, other, payload)
	if not unit or not unit:isAlive() or not payload
		or unit:getAvailableHealth() >= unit:getTotalHealth() then
		return
	end

	local healing = math.floor(actualDamage(payload) * math.max(0, self.val or 0)
		/ BASIS_POINTS_PER_WHOLE)
	if healing <= 0 then return end

	-- Heal only surviving creatures. This does not raise the stack's count.
	local healed = server:healUnit(battle, unit, healing,
		ENUM.HealLevel.heal, ENUM.HealPower.permanent)
	if healed <= 0 then return end

	server:appendLog(battle, {
		append = { "new-horizons.combat.vampirism.healed" },
		replaceStrings = { unit:getCreature():getNameTextID(unit:getCount()) },
		replaceNumbers = { healed }
	})
end

return Script
