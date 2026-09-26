local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local FOCUS_MAGIC_EVENT = "core:focusMagic"
local METAMAGIC_SKILL = "new-horizons:metamagic"
local ARCANE_ACQUISITION_PERK = "new-horizons:metamagic.arcaneAcquisition"
local DURATION_TURNS = 3

function Script:isValidTarget(mechanics, unit)
	if unit == nil or not unit:isAlive() or not unit:isValidTarget(false) or not unit:isShooter() then
		return false
	end

	return mechanics:getBattle():getControllingSide(unit) == mechanics:getCasterSide()
end

function Script:apply(mechanics, server, target)
	local battle = mechanics:getBattle()
	local spellKey = mechanics:getSpell():getJsonKey()
	local casterSide = mechanics:getCasterSide()
	local basisPoints = mechanics:getArcaneBreachMarkBasisPoints()
	local heroCaster = mechanics:getHeroCaster()
	-- Store the provenance on the enchantment itself so it survives saves and
	-- later perk changes. Missing parameters on older enchantments mean false.
	local arcaneAcquisition = mechanics:isMetamagicFollowup()
		and heroCaster ~= nil
		and heroCaster:hasActivePerk(METAMAGIC_SKILL, ARCANE_ACQUISITION_PERK)

	for _, destination in ipairs(target) do
		local unit = destination.unit
		if unit and unit:isAlive() and self:isValidTarget(mechanics, unit) and self:isReceptive(mechanics, unit) then
			local previous = unit:getBonuses({
				type = "COMBAT_EVENT_TRIGGER",
				subtype = FOCUS_MAGIC_EVENT
			}):filter(function(bonus)
				return bonus:getSource() == ENUM.BonusSource.spellEffect
					and bonus:getSourceID() == spellKey
			end)

			if previous:size() > 0 then
				server:removeUnitBonuses(battle, unit, previous)
			end

			server:addUnitBonus(battle, unit, {
				type = "COMBAT_EVENT_TRIGGER",
				subtype = FOCUS_MAGIC_EVENT,
				val = basisPoints,
				duration = ENUM.BonusDuration.nTurns,
				turns = mechanics:adjustEffectDuration(DURATION_TURNS),
				sourceType = ENUM.BonusSource.spellEffect,
				sourceID = spellKey,
				stacking = spellKey,
				addInfo = {
					beneficiarySide = casterSide,
					arcaneAcquisition = arcaneAcquisition
				}
			}, false)
		end
	end
end

return Script
