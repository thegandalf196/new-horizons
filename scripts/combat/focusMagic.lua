local Base = require("combat/combatScript")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

--- Parameters:
---   val - captured Arcane Breach penetration in basis points (10,000 bps = 100%)
---   beneficiarySide - battle side that cast Focus Magic
---   arcaneAcquisition - saved Arcane Acquisition provenance (optional; false when absent)

local ARCANE_BREACH_EVENT = "core:arcaneBreach"
local ARCANE_BREACH_SPELL = "new-horizons:arcaneBreach"
local MAX_MARKS = 3
local MAX_MARK_PENETRATION_BASIS_POINTS = 2000
local MARK_DURATION_TURNS = 2
local MARK_APPLIED_TEXT = "new-horizons.combat.arcaneBreach.applied"
local MARK_REFRESHED_TEXT = "new-horizons.combat.arcaneBreach.refreshed"
local ATTACKER_SIDE_TEXT = "new-horizons.combat.arcaneBreach.side.attacker"
local DEFENDER_SIDE_TEXT = "new-horizons.combat.arcaneBreach.side.defender"

local function logArcaneBreachMark(server, battle, target, markCount, combinedPenetrationBasisPoints,
	beneficiarySide, refreshed)
	local sideText = beneficiarySide == ENUM.BattleSide.attacker and ATTACKER_SIDE_TEXT or DEFENDER_SIDE_TEXT
	local wholePercent = math.floor(combinedPenetrationBasisPoints / 100)
	local tenthsPercent = math.floor(combinedPenetrationBasisPoints / 10) % 10
	local hundredthsPercent = combinedPenetrationBasisPoints % 10
	server:appendLog(battle, {
		append = { refreshed and MARK_REFRESHED_TEXT or MARK_APPLIED_TEXT },
		replaceStrings = {
			target:getCreature():getNameTextID(target:getCount()),
			sideText
		},
		replaceNumbers = {
			markCount,
			MARK_DURATION_TURNS,
			wholePercent,
			tenthsPercent,
			hundredthsPercent
		}
	})
end

local function getArcaneBreachMarks(target)
	return target:getBonuses({
		type = "COMBAT_EVENT_TRIGGER",
		subtype = ARCANE_BREACH_EVENT
	}):filter(function(bonus)
		return bonus:getSource() == ENUM.BonusSource.spellEffect
			and bonus:getSourceID() == ARCANE_BREACH_SPELL
	end)
end

--- Applies or refreshes one target's Arcane Breach marks. Kept separate from event dispatch so
--- authoritative attacks and deterministic hypothetical-battle projections can share the mutation.
function Script.applyArcaneBreachMark(server, battle, target, basisPoints, beneficiarySide, markApplications)
	if target == nil or not target:isAlive() then return end

	local marks = getArcaneBreachMarks(target)

	local mark = {
		type = "COMBAT_EVENT_TRIGGER",
		subtype = ARCANE_BREACH_EVENT,
		val = basisPoints,
		duration = ENUM.BonusDuration.nTurns,
		turns = MARK_DURATION_TURNS,
		sourceType = ENUM.BonusSource.spellEffect,
		sourceID = ARCANE_BREACH_SPELL,
		addInfo = { beneficiarySide = beneficiarySide }
	}

	local markCount = marks:size()
	local previousMarkCount = markCount
	if previousMarkCount > 0 then
		-- A non-cumulative update refreshes every existing mark without changing its captured value
		-- or parameters. Marks intentionally have no stacking key, so getBonuses keeps each one
		-- independently countable even when their captured values happen to match.
		server:addUnitBonus(battle, target, mark, false)
	end

	-- Decide from the target's current mark state at damage time. Arcane Acquisition
	-- can add two marks only while this target is still unmarked.
	local requestedAdditions = markApplications or 1
	local additions = requestedAdditions > 1 and previousMarkCount == 0 and requestedAdditions or 1
	for index = 1, math.min(additions, math.max(0, MAX_MARKS - previousMarkCount)) do
		server:addUnitBonus(battle, target, mark, previousMarkCount > 0 or index > 1)
	end

	if server:describeChanges() then
		local currentMarks = getArcaneBreachMarks(target)
		local combinedPenetrationBasisPoints = 0
		local validMarks = 0
		-- Match the damage callback: retain captured values, but clamp each valid mark and only
		-- include the first three marks for the applying side.
		for index = 1, currentMarks:size() do
			local currentMark = currentMarks:getBonus(index)
			local parameters = currentMark:getParametersAsJson()
			if parameters and parameters.beneficiarySide == beneficiarySide and currentMark:getVal() > 0 then
				local perMarkBasisPoints = math.min(currentMark:getVal(), MAX_MARK_PENETRATION_BASIS_POINTS)
				combinedPenetrationBasisPoints = math.min(
					combinedPenetrationBasisPoints + perMarkBasisPoints,
					MAX_MARKS * MAX_MARK_PENETRATION_BASIS_POINTS)
				validMarks = validMarks + 1
				if validMarks >= MAX_MARKS then break end
			end
		end
		logArcaneBreachMark(server, battle, target, currentMarks:size(), combinedPenetrationBasisPoints,
			beneficiarySide, previousMarkCount >= MAX_MARKS)
	end
end

function Script:onAfterAttack(server, battle, unit, other, payload)
	if not payload.ranged then return end
	if battle:getControllingSide(unit) ~= self.beneficiarySide then return end

	local markedUnits = {}
	for _, target in ipairs(payload.targets or {}) do
		local victim = target.unit
		if victim and victim:isAlive() and target.damage > 0
			and battle:getControllingSide(victim) ~= self.beneficiarySide then
			local unitID = victim:unitID()
			if not markedUnits[unitID] then
				markedUnits[unitID] = true
				local currentMarkCount = getArcaneBreachMarks(victim):size()
				local applications = self.arcaneAcquisition == true and currentMarkCount == 0 and 2 or 1
				Script.applyArcaneBreachMark(server, battle, victim, self.val, self.beneficiarySide, applications)
			end
		end
	end
end

return Script
