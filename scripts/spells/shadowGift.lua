local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local COMBAT_EVENT = "core:shadowGift"
local DURATION_TURNS = 3

function Script:apply(mechanics, server, target)
	local battle = mechanics:getBattle()
	local spellKey = mechanics:getSpell():getJsonKey()
	local sacrificePercent = mechanics:getShadowGiftSacrificePercent()
	local damageBonusBasisPoints = mechanics:getShadowGiftDamageBonusBasisPoints()
	if (sacrificePercent ~= 10 and sacrificePercent ~= 20 and sacrificePercent ~= 30)
		or mechanics:getShadowGiftSacrificeCostBasisPoints() <= 0
		or damageBonusBasisPoints <= 0 then
		return
	end

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
				val = damageBonusBasisPoints,
				duration = ENUM.BonusDuration.nTurns,
				turns = DURATION_TURNS + mechanics:getExtendSpellBonusRounds(),
				sourceType = ENUM.BonusSource.spellEffect,
				sourceID = spellKey,
				stacking = spellKey,
				addInfo = {
					casterSide = mechanics:getCasterSide()
				}
			}, false)
		end
	end
end

return Script
