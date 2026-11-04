local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local DEFENSE_LOSS_BONUS_FILTER = {
	type = "PRIMARY_SKILL",
	subtype = "defence",
	sourceType = ENUM.BonusSource.spellEffect
}

local function frailtyBonuses(unit, spellKey)
	return unit:getBonuses(DEFENSE_LOSS_BONUS_FILTER):filter(function(bonus)
		return bonus:getSourceID() == spellKey
	end)
end

function Script:apply(mechanics, server, target)
	local battle = mechanics:getBattle()
	local spellKey = mechanics:getSpell():getJsonKey()

	for _, destination in ipairs(target) do
		local unit = destination.unit
		if unit and unit:isAlive() and self:isReceptive(mechanics, unit) then
			local previousBonuses = frailtyBonuses(unit, spellKey)
			local plan = mechanics:getRecipientFrailtyPlan(unit)
			local total = plan.totalBasisPoints
			local defenseLoss = plan.defenseLoss

			if previousBonuses:size() > 0 then
				server:removeUnitBonuses(battle, unit, previousBonuses)
			end

			server:addUnitBonus(battle, unit, {
				type = "PRIMARY_SKILL",
				subtype = "defence",
				val = -defenseLoss,
				duration = ENUM.BonusDuration.oneBattle,
				sourceType = ENUM.BonusSource.spellEffect,
				sourceID = spellKey,
				stacking = spellKey,
				statusTags = {"DEBUFF"},
				statusIdentity = spellKey,
				addInfo = total
			}, false)
		end
	end
end

return Script
