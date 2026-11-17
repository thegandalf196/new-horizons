/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ConsecratedStrikeProjection.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "ForecastNativeAdapter.h"
#include "SearchBudget.h"
#include "../../lib/battle/ConsecratedArmsCombat.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../lib/battle/NewHorizonsPassiveSpecialties.h"

namespace consecratedArmsAI
{
namespace
{
bool active(const battle::Unit * unit)
{
	return unit && unit->mayHaveBonusType(BonusType::NONE) && consecratedArms::activePrayerCount(*unit) > 0;
}
bool protectedAfflictionTarget(const CBattleInfoCallback & world, const battle::Unit & source, const battle::Unit & target)
{
	if(!target.mayHaveBonusType(BonusType::NONE)) return false;
	const auto owner = world.battleGetOwner(&source);
	if(owner == PlayerColor::CANNOT_DETERMINE || owner == world.battleGetOwner(&target)) return false;
	const auto prayer = consecratedArms::activePrayer(target, consecratedArms::Prayer::STEADFAST_SOUL);
	return prayer && prayer->policy.has(consecratedArms::Perk::UNSHAKABLE_FAITH);
}
}
int afflictionResistanceBP(const CBattleInfoCallback & world, const battle::Unit & source,
	const battle::Unit & target, creatureAbility::Native identity)
{
	if(!protectedAfflictionTarget(world, source, target)) return 0;
	const auto descriptor = creatureAbility::nativeAffliction(identity, creatureAbility::Family::CREATURE_ABILITY,
		world.battleGetOwner(&source));
	return creatureAbility::unshakableResistanceBP(*descriptor, target);
}
int afflictionResistanceBP(const CBattleInfoCallback & world, const battle::Unit & source,
	const battle::Unit & target, const CSpell & spell)
{
	if(!protectedAfflictionTarget(world, source, target)) return 0;
	return creatureAbility::unshakableResistanceBP(*creatureAbility::onHitSpell(spell, world.battleGetOwner(&source)), target);
}
bool needsStrikeProjection(const CBattleInfoCallback & world, const battle::Unit * actor, const battle::Unit * target)
{
	if(active(actor) || active(target)) return true;
	// Communion can protect a peer outside the selected attack endpoints.
	for(const auto * unit : world.battleGetAllUnits(false))
		if(unit && unit->mayHaveBonusType(BonusType::NONE)
			&& consecratedArms::activePrayer(*unit, consecratedArms::Prayer::VICARIOUS_REDEMPTION)) return true;
	return false;
}
float fearlessCounterCorrection(const battle::Unit & attacker, float utility, float otherFailureProbability)
{
	return utility * consecratedArms::fearlessAdvanceChance(attacker) / 100.0f
		* std::clamp(otherFailureProbability, 0.0f, 1.0f);
}
void restoreShieldState(HypotheticBattle & destination, const HypotheticBattle & captured)
{
	for(const auto * unit : captured.battleGetAllUnits(false))
	{
		const auto * before = dynamic_cast<const battle::CUnitState *>(unit);
		const auto * current = dynamic_cast<const battle::CUnitState *>(destination.battleGetUnitByID(unit->unitId()));
		if(!before || !current || (!before->redemptionShield.initialized && !current->redemptionShield.initialized)) continue;
		destination.getForUpdate(unit->unitId())->redemptionShield = before->redemptionShield;
	}
}
void completeObservedAction(HypotheticBattle & world, uint32_t actor)
{
	auto unit = world.getForUpdate(actor);
	unit->completedActionThisRound = true;
	forecast::NativeAdapter::importReceipt(world, *unit);
}
battle::DamageContext StrikeProjection::Hit::context()
{
	return {damageClass, battle::DamageOrigin::CREATURE_STRIKE, quota ? &*quota : nullptr};
}
StrikeProjection::StrikeProjection(HypotheticBattle * board) : branch(board)
{
	if(branch)
		for(const auto * unit : branch->battleGetAllUnits(false))
		{
			if(!active(unit)) continue;
			enabled = true;
			redemptionEnabled |= consecratedArms::activePrayer(*unit, consecratedArms::Prayer::VICARIOUS_REDEMPTION).has_value();
		}
}
StrikeProjection::Hit StrikeProjection::prepare(const CBattleInfoCallback & world,
	const BattleAttackInfo & attack, const battle::CUnitState & target, int64_t amount, battle::DamageProvenance provenance)
{
	Hit result;
	if(!enabled) return result;
	battleSearch::require(branch->searchBudget().get(), battleSearch::WorkKind::Node);
	result.tracked = true;
	if(attack.retaliation && !attack.shooting && !attack.bracePreemptive
		&& attack.preemptiveDamagePercent <= 0 && attack.defender)
		result.weight -= fearlessCounterCorrection(*attack.defender, 1);
	const auto & current = branch ? static_cast<const CBattleInfoCallback &>(*branch) : world;
	// Probabilistic status marginals use their existing kernels. Deterministic
	// Gaze/Bind and opaque on-hit continuations still use the native nominal
	// reference state; conservatively expose that uncertainty without pruning.
	result.uncertainControl = attack.attacker && protectedAfflictionTarget(current, *attack.attacker, target);
	result.count = target.getCount(); result.maximumHP = target.getMaxHealth(); result.owner = current.battleGetOwner(&target);
	result.damageClass = attack.physicalDamage ? battle::DamageClass::PHYSICAL : battle::DamageClass::MAGICAL;
	if(target.mayHaveBonusType(BonusType::NONE)
		&& consecratedArms::activePrayer(target, consecratedArms::Prayer::UNBROKEN_VOW))
		result.quota = battle::VowCasualtyQuota::capture(target.unitId(), target.getCount(),
			consecratedArms::prayerValues(target, consecratedArms::Prayer::UNBROKEN_VOW).casualtyCeilingBP);
	const auto context = result.context();
	const auto preview = battle::previewShieldDamage(target, amount, provenance, &context);
	if(!preview) throw std::logic_error("Uncertified Consecrated Arms damage cohort");
	if(attack.physicalDamage && !attack.bracePreemptive && attack.preemptiveDamagePercent <= 0
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attack.attacker)
		&& current.battleGetOwner(attack.attacker) != result.owner)
	{
		const auto [potency, inserted] = potencies.try_emplace(attack.attacker->unitId());
		if(inserted) potency->second = consecratedArms::judgmentPotency(*attack.attacker);
		result.judgment = consecratedArms::judgmentRawDamage(potency->second, target, preview->bodyHealthLoss);
	}
	return result;
}
void StrikeProjection::record(const BattleAttackInfo & attack, const battle::CUnitState & target, Hit hit)
{
	if(!enabled || !attack.attacker) return;
	uncertainControl |= hit.uncertainControl;
	const auto actor = attack.attacker->unitId();
	const auto lost = std::max<int64_t>(0, hit.count - target.getCount());
	if(lost && redemptionEnabled) redemption[actor].recordActualLoss(*branch, target.unitId(), hit.owner, lost * hit.maximumHP);
	if(hit.judgment > 0) pending.push_back({actor, target.unitId(), hit.judgment, std::move(hit.quota), hit.weight});
}
void StrikeProjection::finish(uint32_t actor)
{
	if(!enabled) return;
	const auto judgments = std::count_if(pending.begin(), pending.end(), [actor](const Pending & hit) { return hit.actor == actor; });
	const auto visits = judgments + (redemptionEnabled && (judgments || redemption.contains(actor))
		? branch->battleGetAllUnits(false).size() : 0);
	battleSearch::require(branch->searchBudget().get(), battleSearch::WorkKind::Node, static_cast<uint32_t>(visits));
	for(auto & hit : pending)
	{
		if(hit.actor != actor) continue;
		auto target = branch->getForUpdate(hit.target);
		if(!target || !target->alive() || target->isGhost() || target->isInvincible()
			|| newHorizonsPassiveSpecialties::magicalDamageImmune(*target)) continue;
		auto before = std::make_shared<StackWithBonuses>(branch, target.get());
		before->freezeDamageScoringBonusSnapshot();
		const auto owner = branch->battleGetOwner(target.get());
		const auto count = target->getCount();
		const auto maximum = target->getMaxHealth();
		auto amount = newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *target, hit.raw);
		const battle::DamageContext context{battle::DamageClass::MAGICAL, battle::DamageOrigin::CREATURE_ABILITY,
			hit.quota ? &*hit.quota : nullptr};
		target->damageWithContext(amount, false, battle::DamageProvenance::MAGICAL_CREATURE, context);
		const auto lost = std::max<int64_t>(0, count - target->getCount());
		if(lost && redemptionEnabled) redemption[actor].recordActualLoss(*branch, hit.target, owner, lost * maximum);
		const auto loss = before->getAvailableHealth() - target->getAvailableHealth();
		if(loss > 0) observations.push_back({std::move(before), owner, loss, hit.weight});
	}
	std::erase_if(pending, [actor](const Pending & hit) { return hit.actor == actor; });
	potencies.erase(actor);
	if(const auto found = redemption.find(actor); found != redemption.end())
	{
		found->second.complete(*branch, *branch->getServerCallback());
		redemption.erase(found);
	}
}
std::pair<float, float> StrikeProjection::value(DamageCache & cache,
	const std::shared_ptr<CBattleInfoCallback> & source, PlayerColor reference) const
{
	std::pair<float, float> result;
	for(const auto & hit : observations)
	{
		const auto value = hit.weight * AttackPossibility::calculateDamageReduce(nullptr, hit.before.get(), hit.loss, cache, source);
		(hit.owner == reference ? result.first : result.second) += value;
	}
	return result;
}
}
