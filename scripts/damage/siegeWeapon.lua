local Script = setmetatable({}, {__index = Base})
Script.__index = Script

--- Ballista deals additional damage based on hero attack
--- Only bonuses from hero itself (base stats) and from equipped artifacts are included
function Script:getBallistaDamageRange(info, minDamage, maxDamage)
	-- Canonical rules defer their absolute output until the whole-stack hook
	-- below and never inherit the retired hero-Attack multiplier.
	if (info.machineBaseDamage or 0) > 0 then
		return minDamage, maxDamage
	end

	local siegeSkillMultiplier = info.siegeSkillMultiplier or 0
	if siegeSkillMultiplier > 0 then
		return minDamage * siegeSkillMultiplier, maxDamage * siegeSkillMultiplier
	end

	local heroAttack = info.attacker:getBonusesValue({type = "PRIMARY_SKILL", subtype = "attack", sourceType = ENUM.BonusSource.artifact})
		+ info.attacker:getBonusesValue({type = "PRIMARY_SKILL", subtype = "attack", sourceType = ENUM.BonusSource.heroBaseSkill})

	return minDamage * (heroAttack + 1), maxDamage * (heroAttack + 1)
end

function Script:getBaseDamageSingle(info)
	local minDamage, maxDamage = Base.getBaseDamageSingle(self, info)

	if not self:hasBonusOfType(info.attackerBonuses, "SIEGE_WEAPON") then return minDamage, maxDamage end
	if info.attacker:isTurret() then return minDamage, maxDamage end

	return self:getBallistaDamageRange(info, minDamage, maxDamage)
end

--- Canonical Siege output is absolute per machine activation.
function Script:getBaseDamage(info)
	local minDamage, maxDamage = Base.getBaseDamage(self, info)

	if not self:hasBonusOfType(info.attackerBonuses, "SIEGE_WEAPON") then return minDamage, maxDamage end

	local machineBaseDamage = info.machineBaseDamage or 0
	if machineBaseDamage <= 0 then return minDamage, maxDamage end

	return machineBaseDamage, machineBaseDamage
end

Script:declareBonus("PRIMARY_SKILL")
Script:declareBonus("SIEGE_WEAPON")

return Script
