local Base = require("spells/unitEffect")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

function Script:getDispelableBonuses(mechanics, unit)
	local currentSpellKey = mechanics:getSpell():getJsonKey()
	if mechanics:isNewHorizonsResurrection() then
		-- New Horizons Resurrection restores casualties only; the optional
		-- legacy Cure sub-effect is not part of this saved spell behavior.
		return unit:getBonuses({}):filter(function() return false end)
	end
	if mechanics:isNewHorizonsCure() then
		if mechanics:getCurePhysicalAffliction() ~= "" then
			-- Physical creature-source markers are removed by their typed plan,
			-- never by treating a creature ID as a spell source.
			return unit:getBonuses({}):filter(function() return false end)
		end
		local selectedAffliction = mechanics:getCureAfflictionSource()
		-- Cure removes only the explicitly selected physical-affliction source
		-- group. The shared C++ preflight validates that this ID is present in
		-- the saved allowlist and still exists on the exact target.
		return unit:getBonuses({}):filter(function(bonus)
			return selectedAffliction ~= ""
				and bonus:getSource() == ENUM.BonusSource.spellEffect
				and bonus:getSourceID() == selectedAffliction
		end)
	end
	local selective = mechanics:isSelectiveDispel()
	local friendly = selective and mechanics:ownerIsSameAs(unit)
	-- no filter describes this: what makes a bonus dispelable is the spell that granted it
	return unit:getBonuses({}):filter(function(bonus)
		if bonus:getSource() ~= ENUM.BonusSource.spellEffect then return false end
		if bonus:getSourceID() == currentSpellKey then return false end
		local sourceSpell = LIBRARY:getSpellByName(bonus:getSourceID())
		if not sourceSpell then return false end
		if sourceSpell:isPersistent() then return false end
		if sourceSpell:isAdventure()  then return false end
		if selective then
			if friendly and sourceSpell:isNegative() then return true end
			if not friendly and sourceSpell:isPositive() then return true end
			return false
		end
		if self.dispelPositive and sourceSpell:isPositive() then return true end
		if self.dispelNegative and sourceSpell:isNegative() then return true end
		if self.dispelNeutral  and sourceSpell:isNeutral()  then return true end
		return false
	end)
end

function Script:isValidTarget(mechanics, unit)
	if not unit:isValidTarget(false) then return false end
	if mechanics:isNewHorizonsCure() and mechanics:getCurePhysicalAffliction() ~= "" then
		return mechanics:isSelectedCurePhysicalAfflictionTarget(unit)
	end
	return self:getDispelableBonuses(mechanics, unit):size() > 0
end

function Script:apply(mechanics, server, target)
	local battle       = mechanics:getBattle()
	local positiveOnly = self.dispelPositive and not self.dispelNegative and not self.dispelNeutral

	for _, dest in ipairs(target) do
		local unit = dest.unit
		if unit then
			if mechanics:isNewHorizonsCure() and mechanics:getCurePhysicalAffliction() ~= "" then
				server:removeCurePhysicalAffliction(battle, unit, mechanics:getCurePhysicalAffliction())
			end
			local bonuses = self:getDispelableBonuses(mechanics, unit)
			if bonuses:size() > 0 then
				if positiveOnly and server:describeChanges() then
					server:appendLog(battle, {
						append         = { "core.genrltxt.555" },
						replaceStrings = { unit:getCreature():getNameTextID(0) }
					})
				end
				server:removeUnitBonuses(battle, unit, bonuses)
			end
		end
	end
end

return Script
