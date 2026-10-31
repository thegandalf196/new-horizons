local Revision = {}

function Revision.addSlowSpeed(mechanics, buffer)
	if not mechanics:slowAffectsSpeed() then return end
	for _, bonus in pairs(buffer) do
		if bonus.type == "STACKS_INITIATIVE" then
			local speed = {}
			for key, value in pairs(bonus) do speed[key] = value end
			speed.type = "STACKS_SPEED"
			speed.valueType = "PERCENT_TO_ALL"
			buffer.slowSpeed = speed
			return
		end
	end
end

function Revision.scaleSlow(mechanics, buffer, spellKey)
	if spellKey ~= "core:slow" then return end
	local powerPercent = mechanics:isMassSlow() and 60 or mechanics:getVariantPowerPercent()
	if powerPercent >= 100 then return end
	local affectsSpeed = mechanics:slowAffectsSpeed()
	for _, bonus in pairs(buffer) do
		if (bonus.type == "STACKS_INITIATIVE" or (affectsSpeed and bonus.type == "STACKS_SPEED"))
			and (bonus.val or 0) < 0 then
			-- Scale after target-specific specialties, rounding toward zero.
			bonus.val = mechanics:scaleSlowVariantValue(bonus.val, powerPercent)
		end
	end
end

function Revision.refreshSlow(mechanics, server, battle, unit, spellKey)
	if spellKey ~= "core:slow" or not mechanics:usesNewHorizonsMagicV3() then return end
	local affectsSpeed = mechanics:slowAffectsSpeed()
	local previous = unit:getBonuses({}):filter(function(bonus)
		return bonus:getSource() == ENUM.BonusSource.spellEffect and bonus:getSourceID() == spellKey
			and (bonus:getType() == "STACKS_INITIATIVE"
				or (affectsSpeed and bonus:getType() == "STACKS_SPEED"))
	end)
	if previous:size() > 0 then server:removeUnitBonuses(battle, unit, previous) end
end

return Revision
