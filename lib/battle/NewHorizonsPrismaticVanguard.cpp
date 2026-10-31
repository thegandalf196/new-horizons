/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsPrismaticVanguard.h"
#include "BattleInfo.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../CStack.h"
#include "../CCreatureHandler.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include "../entities/hero/CHero.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../mapObjects/CGHeroInstance.h"
#include <stdexcept>
namespace newHorizonsPrismaticVanguard
{
namespace
{
constexpr auto identity = "new-horizons:ordinary-creature-specialty:core:monere:prismaticVanguard";
bool wisp(CreatureID creature)
{
	const auto * registered = creature.hasValue() ? creature.toCreature() : nullptr;
	if(!registered)
		return false;
	const auto & creatureIdentity = registered->getJsonKey();
	return creatureIdentity == "new-horizons:wisp" || creatureIdentity == "new-horizons:wispUpgrade";
}
}
bool isEffect(const Bonus * bonus)
{
	if(!bonus || bonus->stacking != identity)
		return false;
	const bool pending = bonus->duration == BonusDuration::ONE_BATTLE;
	if(bonus->source != BonusSource::HERO_SPECIAL
		|| bonus->sid != BonusSourceID(HeroTypeID(HeroTypeID::decode("core:monere")))
		|| bonus->type != BonusType::NONE || bonus->subtype != BonusSubtypeID()
		|| (!pending && bonus->duration != BonusDuration::STACK_ACTIVATION)
		|| bonus->valType != BonusValueType::BASE_NUMBER || bonus->val <= 0 || bonus->val > 100
		|| bonus->targetSourceType != BonusSource::OTHER || bonus->turnsRemain != 0
		|| bonus->effectRange != BonusLimitEffect::NO_LIMIT || bonus->appliedByEnemy
		|| bonus->bonusOwner != PlayerColor::CANNOT_DETERMINE
		|| bonus->spellCasterOwner != PlayerColor::CANNOT_DETERMINE
		|| bonus->statusIdentity != (pending ? "prismaticVanguardPending" : "prismaticVanguard")
		|| bonus->statusTags != std::vector<BonusStatusTag>{BonusStatusTag::NON_TRANSFERABLE}
		|| bonus->parameters || bonus->limiter || bonus->propagator || bonus->updater || bonus->propagationUpdater
		|| !bonus->customIconPath.empty())
		throw std::runtime_error("Invalid Prismatic Vanguard specialty provenance");
	return true;
}
std::optional<Bonus> entryBonus(const CGHeroInstance * hero, CreatureID creature)
{
	if(!hero || !hero->getHeroType() || hero->getHeroType()->getJsonKey() != "core:monere"
		|| !newHorizonsHeroes::hasOrdinaryCreatureSpecialty(hero))
		return std::nullopt;
	const auto & policy = hero->getPrimaryGrowthRules()["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	const int amount = static_cast<int>(policy["prismaticVanguardPhysicalDamagePercent"].Integer());
	if(amount <= 0 || !wisp(creature))
		return std::nullopt;
	Bonus result(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::HERO_SPECIAL,
		amount, BonusSourceID(HeroTypeID(HeroTypeID::decode("core:monere"))),
		BonusSubtypeID(), BonusValueType::BASE_NUMBER);
	result.stacking = identity;
	result.statusIdentity = "prismaticVanguardPending";
	result.statusTags = {BonusStatusTag::NON_TRANSFERABLE};
	result.description.appendRawString("Prismatic Vanguard: +" + std::to_string(amount)
		+ "% physical attack damage during this body's first real activation.");
	isEffect(&result);
	return result;
}
void captureInitialEntries(BattleInfo & battle)
{
	for(const auto & stack : battle.stacks)
		if(stack)
			if(const auto entry = entryBonus(battle.getSideHero(stack->unitSide()), stack->creatureId()))
				stack->addNewBonus(std::make_shared<Bonus>(*entry));
}
ActivationPlan activationPlan(const CBattleInfoCallback & battle, const battle::Unit & unit,
	BattleUnitTurnReason reason)
{
	ActivationPlan result;
	if(!unit.alive() || unit.isGhost() || !battle.battleBeginsActivation(&unit, reason))
		return result;
	const auto markers = unit.getUnstackedBonuses(CSelector(isEffect));
	if(markers->size() > 1)
		throw std::runtime_error("Duplicate Prismatic Vanguard entitlement");
	for(const auto & marker : *markers)
	{
		if(marker->duration != BonusDuration::ONE_BATTLE)
			continue;
		result.remove.push_back(*marker);
		if(wisp(unit.creatureId()))
		{
			auto active = *marker;
			active.duration = BonusDuration::STACK_ACTIVATION;
			active.statusIdentity = "prismaticVanguard";
			result.add.push_back(std::move(active));
		}
	}
	return result;
}
int activeDamagePercent(const BattleAttackInfo & attack)
{
	if(!attack.physicalDamage || !attack.attacker || !attack.attacker->alive()
		|| attack.attacker->isGhost())
		return 0;
	const auto markers = attack.attacker->getUnstackedBonuses(CSelector(isEffect));
	if(markers->size() > 1)
		throw std::runtime_error("Duplicate Prismatic Vanguard entitlement");
	if(markers->empty() || markers->front()->duration != BonusDuration::STACK_ACTIVATION)
		return 0;
	return wisp(attack.attacker->creatureId()) ? markers->front()->val : 0;
}
}
