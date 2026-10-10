local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local COMBAT_EVENT = "core:hexOfPain"
local DURATION_TURNS = 3
local DAMAGE_SHARE_PERCENT = 10
function Script:apply(mechanics, server, target)
	local battle = mechanics:getBattle()
	local spellKey = mechanics:getSpell():getJsonKey()
	local flatDamage = mechanics:getHexOfPainFlatDamage()
	local duration = mechanics:adjustEffectDuration(DURATION_TURNS)
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
				val = flatDamage,
				duration = ENUM.BonusDuration.nTurns,
				turns = duration,
				sourceType = ENUM.BonusSource.spellEffect,
				sourceID = spellKey,
				stacking = spellKey,
				statusTags = {"DEBUFF"},
				statusIdentity = spellKey,
				addInfo = {
					damageSharePercent = DAMAGE_SHARE_PERCENT,
					casterSide = casterSide
				}
			}, false)
		end
	end
end

return Script
