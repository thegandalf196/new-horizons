/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleConsecratedArms.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/BodyHealthLoss.h"
#include "../../lib/battle/ConsecratedArmsCombat.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../lib/battle/NewHorizonsPassiveSpecialties.h"
#include "../../lib/battle/UnitShieldState.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include <algorithm>
#include <stdexcept>

namespace battleConsecratedArms
{
battle::DamageContext damageContext(AttackContext & attack, const battle::Unit & target, bool physical)
{
	battle::VowCasualtyQuota * quota = nullptr;
	if(consecratedArms::activePrayer(target, consecratedArms::Prayer::UNBROKEN_VOW))
	{
		const auto [entry, inserted] = attack.vow.try_emplace(target.unitId(),
			battle::VowCasualtyQuota::capture(target.unitId(), target.getCount(),
				consecratedArms::prayerValues(target, consecratedArms::Prayer::UNBROKEN_VOW).casualtyCeilingBP));
		quota = &entry->second;
	}
	return {physical ? battle::DamageClass::PHYSICAL : battle::DamageClass::MAGICAL,
		battle::DamageOrigin::CREATURE_STRIKE, quota};
}

std::vector<Judgment> prepareJudgment(const CBattleInfoCallback & battle,
	const battle::Unit & source, bool ordinaryStrike, const std::vector<BattleStackAttacked> & hits,
	AttackContext & attack)
{
	std::vector<Judgment> result;
	const auto potency = consecratedArms::judgmentPotency(source);
	if(!ordinaryStrike || !potency.basisPoints || !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(&source))
		return result;
	for(const auto & hit : hits)
	{
		const auto * target = battle.battleGetUnitByID(hit.stackAttacked);
		const auto * before = dynamic_cast<const battle::CUnitState *>(target);
		if(!target || !target->alive() || !before || hit.damageAmount <= 0
			|| battle.battleGetOwner(target) == battle.battleGetOwner(&source))
			continue;
		const int64_t bodyLoss = battle::bodyHealthLoss(hit.damageAmount, before->getHealth().getTemporaryHitPoints());
		const int64_t raw = consecratedArms::judgmentRawDamage(potency, *target, bodyLoss);
		if(raw > 0)
		{
			const auto quota = attack.vow.find(target->unitId());
			result.push_back({source.unitId(), target->unitId(), raw,
				quota == attack.vow.end() ? nullptr : &quota->second});
		}
	}
	return result;
}

void applyJudgment(CGameHandler & handler, const CBattleInfoCallback & battle,
	const std::vector<Judgment> & prepared, AttackContext & attack)
{
	StacksInjured injuries;
	injuries.battleID = battle.getBattle()->getBattleID();
	for(const auto & judgment : prepared)
	{
		const auto * target = battle.battleGetUnitByID(judgment.target);
		if(!target || !target->alive() || target->isGhost() || target->isInvincible()
			|| newHorizonsPassiveSpecialties::magicalDamageImmune(*target))
			continue;
		const int64_t damage = newHorizonsMagicalAbilityDamage::adjustDamage(battle, *target, judgment.rawMagicalDamage);
		if(damage <= 0)
			continue;
		BattleStackAttacked hit;
		hit.attackerID = judgment.source;
		hit.stackAttacked = judgment.target;
		hit.damageAmount = damage;
		hit.flags = BattleStackAttacked::SPELL_EFFECT;
		auto state = target->acquireState();
		const battle::DamageContext context{battle::DamageClass::MAGICAL,
			battle::DamageOrigin::CREATURE_ABILITY, judgment.vow};
		CStack::prepareAttacked(hit, handler.getRandomGenerator(), state, false, false,
			battle::DamageProvenance::MAGICAL_CREATURE, nullptr, &context);
		injuries.stacks.push_back(std::move(hit));
	}
	if(!injuries.stacks.empty())
	{
		attack.redemption.record(battle, injuries.stacks);
		handler.sendAndApply(injuries);
	}
}

bool fearlessAdvance(CGameHandler & handler, const CBattleInfoCallback & battle,
	const battle::Unit & attacker)
{
	const int chance = consecratedArms::fearlessAdvanceChance(attacker);
	if(chance == 0 || handler.getRandomGenerator().nextInt(99) >= chance)
		return false;
	BattleLogMessage message;
	message.battleID = battle.getBattle()->getBattleID();
	MetaString line;
	line.appendRawString("Fearless Advance prevents retaliation against ");
	line.appendRawString(attacker.getDescription());
	line.appendRawString(".");
	message.lines.push_back(std::move(line));
	handler.sendAndApply(message);
	return true;
}
}
