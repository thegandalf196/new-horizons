/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSpecialtyEvents.h"
#include "SpecialtyEventPolicy.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../entities/hero/CHero.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../bonuses/BonusList.h"
#include <limits>
#include <array>
namespace newHorizonsSpecialtyEvents
{
namespace
{
constexpr auto UNDERTOW = "new-horizons:ordinary-creature-specialty:core:lacus:undertow";
constexpr auto FRENZY = "new-horizons:ordinary-creature-specialty:core:gretchin:goblinFrenzySpent";
bool ordinary(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	return attack.attacker && attack.defender && attack.attacker->alive() && attack.defender->alive()
		&& !attack.attacker->isGhost() && !attack.defender->isGhost() && !attack.secondaryAttack
		&& !attack.bracePreemptive && attack.preemptiveDamagePercent == 0 && attack.cleaveDamagePercent == 0
		&& battle.battleGetOwner(attack.attacker) != battle.battleGetOwner(attack.defender);
}
// Eligibility is owner-defined: irrelevant families need no ordinary/family bonus query.
bool visibleOwnerIs(const CBattleInfoCallback & battle, const battle::Unit * actor, const char * identity)
{
	if(!actor) return false;
	const auto * hero = newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(battle, *actor);
	return hero && hero->getHeroType() && hero->getHeroType()->getJsonKey() == identity;
}
enum class ReceiptKind { Undertow, Frenzy };
BonusSourceID source(ReceiptKind kind)
{
	static const std::array<BonusSourceID, 2> ids{
		BonusSourceID(HeroTypeID(HeroTypeID::decode("core:lacus"))),
		BonusSourceID(HeroTypeID(HeroTypeID::decode("core:gretchin")))};
	return ids.at(static_cast<size_t>(kind));
}
Bonus marker(BonusType type, int value, ReceiptKind kind, const char * identity)
{
	Bonus result(BonusDuration::ONE_BATTLE, type, BonusSource::HERO_SPECIAL, value,
		source(kind));
	result.valType = BonusValueType::BASE_NUMBER;
	result.stacking = identity;
	result.statusIdentity = identity;
	result.statusTags = {BonusStatusTag::NON_TRANSFERABLE};
	return result;
}
bool plainReceipt(const Bonus & bonus)
{
	return bonus.targetSourceType == BonusSource::OTHER && bonus.turnsRemain == 0
		&& bonus.effectRange == BonusLimitEffect::NO_LIMIT && !bonus.appliedByEnemy
		&& bonus.bonusOwner == PlayerColor::CANNOT_DETERMINE
		&& bonus.spellCasterOwner == PlayerColor::CANNOT_DETERMINE
		&& !bonus.parameters && !bonus.limiter && !bonus.propagator && !bonus.updater
		&& !bonus.propagationUpdater && bonus.customIconPath.empty();
}
}
bool isUndertowDefense(const Bonus * bonus)
{
	if(!bonus || bonus->stacking != UNDERTOW) return false;
	if(bonus->source != BonusSource::HERO_SPECIAL || bonus->sid != source(ReceiptKind::Undertow)
		|| bonus->type != BonusType::PRIMARY_SKILL || bonus->subtype != BonusSubtypeID(PrimarySkill::DEFENSE)
		|| bonus->duration != BonusDuration::ONE_BATTLE || bonus->val <= 0
		|| bonus->valType != BonusValueType::ADDITIVE_VALUE || bonus->statusIdentity != UNDERTOW
		|| (bonus->statusTags.size() != 1 || bonus->statusTags.front() != BonusStatusTag::NON_TRANSFERABLE)
		|| !plainReceipt(*bonus))
		throw std::runtime_error("Invalid Undertow Defense receipt");
	return true;
}
bool isGoblinFrenzySpent(const Bonus * bonus)
{
	if(!bonus || bonus->stacking != FRENZY) return false;
	if(bonus->source != BonusSource::HERO_SPECIAL || bonus->sid != source(ReceiptKind::Frenzy)
		|| bonus->type != BonusType::NONE || bonus->duration != BonusDuration::ONE_BATTLE || bonus->val != 1
		|| bonus->subtype != BonusSubtypeID()
		|| bonus->valType != BonusValueType::BASE_NUMBER
		|| bonus->statusIdentity != FRENZY || (bonus->statusTags.size() != 1 || bonus->statusTags.front() != BonusStatusTag::NON_TRANSFERABLE)
		|| !plainReceipt(*bonus))
		throw std::runtime_error("Invalid Goblin Frenzy receipt");
	return true;
}

bool isReceipt(const Bonus * bonus) { return isUndertowDefense(bonus) || isGoblinFrenzySpent(bonus); }
bool needsStrikeEvents(const CBattleInfoCallback & battle, const battle::Unit * actor, const battle::Unit * target)
{
	using namespace detail;
	return (actor && family(*actor, Family::Goblin) && !actor->hasBonus(CSelector(isGoblinFrenzySpent))
		&& coefficient(battle, *actor, "core:gretchin", "goblinFrenzyDamagePercent") > 0)
		|| (target && family(*target, Family::Water)
			&& coefficient(battle, *target, "core:lacus", "undertowDefensePerAttack") > 0);
}
int firstStrikeFinalMultiplier(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!attack.defender || !visibleOwnerIs(battle, attack.attacker, "core:gretchin")) return 100;
	if(!ordinary(battle, attack) || !detail::family(*attack.attacker, detail::Family::Goblin)
		|| attack.attacker->hasBonus(CSelector(isGoblinFrenzySpent))) return 100;
	return 100 + detail::coefficient(battle, *attack.attacker, "core:gretchin", "goblinFrenzyDamagePercent");
}
std::vector<newHorizonsOrdinaryStrike::EffectPlan> acceptedStrike(
	const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	std::vector<newHorizonsOrdinaryStrike::EffectPlan> result;
	if(!ordinary(battle, attack)) return result;
	if(firstStrikeFinalMultiplier(battle, attack) > 100)
	{
		auto spent = marker(BonusType::NONE, 1, ReceiptKind::Frenzy, FRENZY);
		newHorizonsOrdinaryStrike::EffectPlan plan;
		plan.target = attack.attacker->unitId();
		plan.add.push_back(std::move(spent));
		result.push_back(std::move(plan));
	}
	const int increment = detail::family(*attack.defender, detail::Family::Water)
		? detail::coefficient(battle, *attack.defender, "core:lacus", "undertowDefensePerAttack") : 0;
	if(increment > 0)
	{
		newHorizonsOrdinaryStrike::EffectPlan plan;
		plan.target = attack.defender->unitId();
		int64_t value = increment;
		const auto previousDefense = attack.defender->getUnstackedBonuses(CSelector(isUndertowDefense));
		for(const auto & old : *previousDefense)
		{
			plan.remove.push_back(*old);
			value += old->val;
		}
		auto defense = marker(BonusType::PRIMARY_SKILL,
			static_cast<int>(std::min<int64_t>(value, std::numeric_limits<int32_t>::max())), ReceiptKind::Undertow, UNDERTOW);
		defense.subtype = BonusSubtypeID(PrimarySkill::DEFENSE);
		defense.valType = BonusValueType::ADDITIVE_VALUE;
		plan.add.push_back(std::move(defense));
		result.push_back(std::move(plan));
	}
	return result;
}
bool requiresTravelReceipt(const CBattleInfoCallback & battle, const battle::Unit & actor)
{
	return visibleOwnerIs(battle, &actor, "core:korbac")
		&& detail::family(actor, detail::Family::Flies)
		&& detail::coefficient(battle, actor, "core:korbac", "evasiveFlightReductionPercentPerHex") > 0;
}
int retaliationReduction(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!ordinary(battle, attack) || !attack.physicalDamage || !attack.retaliation || attack.shooting
		|| attack.defenderVoluntaryTravelDistance <= 0 || !detail::family(*attack.defender, detail::Family::Flies)) return 0;
	const int perHex = detail::coefficient(battle, *attack.defender, "core:korbac", "evasiveFlightReductionPercentPerHex");
	return static_cast<int>(std::min<int64_t>(100, int64_t(perHex) * attack.defenderVoluntaryTravelDistance));
}
int sorshaReduction(const CBattleInfoCallback & battle, const battle::Unit & actor)
{
	if(!visibleOwnerIs(battle, &actor, "core:sorsha")) return 0;
	return detail::family(actor, detail::Family::Swordsman)
		? detail::coefficient(battle, actor, "core:sorsha", "shieldAndSwordPhysicalReductionPercent") : 0;
}
}
