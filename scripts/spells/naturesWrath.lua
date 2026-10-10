local Base = require("spells/unitEffect")
local Damage = require("spells/damage")
local BattleLog = require("battleLog")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

-- The shared Mechanics route/power APIs deliberately own proximity, stable
-- unit-ID ties, Worldroot and rank scaling. This keeps authoritative casts,
-- detached AI and route previews on one immutable ordered chain.
local function validConductor(unit)
	return unit ~= nil and unit:isAlive() and not unit:isGhost()
		and not unit:isTurret() and not unit:isTimeStopped()
		and unit:getPosition():isAvailable()
end

local function firstTarget(mechanics, target)
	if target == nil or #target ~= 1 then return nil end
	local destination = target[1]
	if destination.unit ~= nil then return destination.unit end
	if destination.hex ~= nil and destination.hex:isAvailable() then
		return mechanics:getBattle():getUnitByPos(destination.hex, true)
	end
	return nil
end

local function noTarget(mechanics, problem)
	if problem ~= nil then problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget) end
	return false
end

function Script:isValidTarget(mechanics, unit)
	return validConductor(unit)
end

function Script:isReceptive(mechanics, unit)
	-- Routing is not effect eligibility. An immune/invincible living stack is
	-- still a conductor and consumes its hop, even though its effect is zero.
	return validConductor(unit)
end

function Script:applicableGeneral(mechanics, problem)
	if #mechanics:getBattle():getUnitsIf(validConductor) > 0 then return true end
	return noTarget(mechanics, problem)
end

function Script:applicableTarget(mechanics, problem, target)
	if validConductor(firstTarget(mechanics, target)) then return true end
	return noTarget(mechanics, problem)
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	local first = firstTarget(mechanics, aimPoint)
	if first == nil then first = firstTarget(mechanics, spellTarget) end
	if not validConductor(first) then return {} end
	return mechanics:getNaturesWrathRoute(first)
end

function Script:filterTarget(mechanics, target)
	-- Global canonical cast filtering preserves a blocked recipient as a
	-- hex-only no-effect slot. Do not compact it or renumber later hops.
	return target
end

local function eligibleEffect(self, mechanics, unit)
	if not validConductor(unit) or unit:isInvincible() then return false end
	if mechanics:ownerIsSameAs(unit) then
		return Base.isReceptive(self, mechanics, unit)
	end
	return Damage.isReceptive(self, mechanics, unit) and not mechanics:wouldResist(unit)
end

local function hopHealthChange(self, mechanics, destination, hopIndex)
	local unit = destination.unit
	if not eligibleEffect(self, mechanics, unit) then return 0, 0 end
	local copy = unit:copy()
	if mechanics:ownerIsSameAs(unit) then
		local amount = mechanics:applySpellBonus(mechanics:getNaturesWrathHopPower(hopIndex), unit)
		local healed = copy:heal(amount, ENUM.HealLevel.heal, ENUM.HealPower.permanent)
		return healed, 0
	end
	local healthBefore = copy:getAvailableHealth()
	local countBefore = copy:getCount()
	copy:damage(mechanics:getNaturesWrathDamage(unit, hopIndex))
	return copy:getAvailableHealth() - healthBefore, copy:getCount() - countBefore
end

function Script:getHealthChange(mechanics, spellTarget)
	local result = { hpDelta = 0, unitsDelta = 0 }
	for index, destination in ipairs(spellTarget) do
		local hpDelta, unitsDelta = hopHealthChange(self, mechanics, destination, index - 1)
		result.hpDelta = result.hpDelta + hpDelta
		result.unitsDelta = result.unitsDelta + unitsDelta
	end
	return result
end

function Script:apply(mechanics, server, target)
	local battle = mechanics:getBattle()
	-- The full chain has already been captured before any damage/healing. Death,
	-- immunity and resisted hops cannot reroute it or restart its attenuation.
	for index, destination in ipairs(target) do
		local unit = destination.unit
		if eligibleEffect(self, mechanics, unit) then
			if mechanics:ownerIsSameAs(unit) then
				local amount = mechanics:applySpellBonus(mechanics:getNaturesWrathHopPower(index - 1), unit)
				local healed = server:healUnit(battle, unit, amount, ENUM.HealLevel.heal, ENUM.HealPower.permanent)
				if healed > 0 and server:describeChanges() then
					server:appendLog(battle, {
						append = { "new-horizons.combat.naturesWrath.healed" },
						replaceStrings = { unit:getCreature():getNameTextID(unit:getCount()) },
						replaceNumbers = { healed }
					})
				end
			else
				local unitID = unit:unitID()
				local damage, killed = server:damageUnit(battle, unit,
					mechanics:getNaturesWrathDamage(unit, index - 1), false, mechanics:getUnitCaster(), true)
				local current = battle:getUnitByID(unitID)
				if current then server:clearFrozenAfterDirectMagicDamage(battle, current) end
				if damage > 0 and server:describeChanges() then
					BattleLog.spellDamage(server, battle, mechanics:getSpell(), unit, damage, killed)
				end
			end
		end
	end
end

return Script
