local Base = require("spells/unitEffect")
local Damage = require("spells/damage")
local BattleLog = require("battleLog")
local Script = setmetatable({}, {__index = Base})
Script.__index = Script

local function validRecipient(unit)
	return unit ~= nil and unit:isAlive() and not unit:isGhost()
		and not unit:isTurret() and unit:getPosition():isAvailable()
end

function Script:isValidTarget(mechanics, unit)
	return validRecipient(unit)
end

function Script:isReceptive(mechanics, unit)
	-- Preserve the complete recipient set until its counts have been captured.
	return validRecipient(unit)
end

function Script:applicableGeneral(mechanics, problem)
	if #mechanics:getPandemoniumTargets() > 0 then return true end
	if problem ~= nil then problem:addStandard(mechanics, ENUM.SpellCastProblem.noAppropriateTarget) end
	return false
end

function Script:applicableTarget(mechanics, problem, target)
	return self:applicableGeneral(mechanics, problem)
end

function Script:transformTarget(mechanics, aimPoint, spellTarget)
	return mechanics:getPandemoniumTargets()
end

function Script:filterTarget(mechanics, target)
	return target
end

local function capturedRecipients(mechanics, target)
	local result = {}
	for _, destination in ipairs(target) do
		local unit = destination.unit
		if validRecipient(unit) then
			table.insert(result, {
				unit = unit,
				count = mechanics:getPandemoniumDebuffCount(unit)
			})
		end
	end
	return result
end

local function eligible(self, mechanics, unit)
	return validRecipient(unit) and not unit:isInvincible()
		and Damage.isReceptive(self, mechanics, unit) and not mechanics:wouldResist(unit)
end

function Script:getHealthChange(mechanics, spellTarget)
	local result = {hpDelta = 0, unitsDelta = 0}
	local recipients = capturedRecipients(mechanics, spellTarget)
	for _, entry in ipairs(recipients) do
		if entry.count > 0 and eligible(self, mechanics, entry.unit) then
			local copy = entry.unit:copy()
			local beforeHP, beforeCount = copy:getAvailableHealth(), copy:getCount()
			copy:damage(mechanics:getPandemoniumDamage(entry.unit, entry.count))
			result.hpDelta = result.hpDelta + copy:getAvailableHealth() - beforeHP
			result.unitsDelta = result.unitsDelta + copy:getCount() - beforeCount
		end
	end
	return result
end

function Script:apply(mechanics, server, target)
	local battle = mechanics:getBattle()
	-- No damage event can change a later recipient's captured D, even if it
	-- removes an aura, status, controller or the source of a physical affliction.
	local recipients = capturedRecipients(mechanics, target)
	for _, entry in ipairs(recipients) do
		if entry.count > 0 and eligible(self, mechanics, entry.unit) then
			local damage, killed = server:damageUnit(battle, entry.unit,
				mechanics:getPandemoniumDamage(entry.unit, entry.count),
				false, mechanics:getUnitCaster(), true)
			server:clearFrozenAfterDirectMagicDamage(battle, entry.unit)
			if damage > 0 and server:describeChanges() then
				BattleLog.spellDamage(server, battle, mechanics:getSpell(), entry.unit, damage, killed)
			end
		end
	end
end

return Script
