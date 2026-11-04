local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local COMBAT_EVENT = "core:hexOfPain"
function Script:apply(mechanics, server, target)
	local battle = mechanics:getBattle()
	local spellKey = mechanics:getSpell():getJsonKey()
	local flatDamage = mechanics:getHexOfPainFlatDamage()
	local duration = mechanics:getScriptedRoundStatusDuration()
	local casterSide = mechanics:getCasterSide()

	for _, destination in ipairs(target) do
		local unit = destination.unit
		if unit and unit:isAlive() and mechanics:ownerMatches(unit) and self:isReceptive(mechanics, unit) then
			local previous = unit:getBonuses({
				type = "COMBAT_EVENT_TRIGGER",
				subtype = COMBAT_EVENT
			}):filter(function(bonus)
				return bonus:getSource() == ENUM.BonusSource.spellEffect
					and bonus:getSourceID() == spellKey
			end)

			if previous:size() > 0 then
				server:removeUnitBonuses(battle, unit, previous)
			end

			server:addUnitBonus(battle, unit, {
				type = "COMBAT_EVENT_TRIGGER",
				subtype = COMBAT_EVENT,
				val = mechanics:adjustMetamagicMagnitude(flatDamage, unit),
				duration = ENUM.BonusDuration.nTurns,
				turns = duration,
				sourceType = ENUM.BonusSource.spellEffect,
				sourceID = spellKey,
				stacking = spellKey,
				statusTags = {"DEBUFF"},
				statusIdentity = spellKey,
				addInfo = {
					damageSharePercent = mechanics:getRecipientHexOfPainDamageSharePercent(unit),
					casterSide = casterSide
				}
			}, false)
		end
	end
end

return Script
