/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsAcademySpecialties.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsSerpentsEmbrace.h"
#include "Unit.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include "../entities/hero/NewHorizonsAcademySpecialties.h"
#include "../mapObjects/CGHeroInstance.h"

namespace newHorizonsAcademy
{
std::optional<Bonus> hardenedMarker(const CGHeroInstance * hero, const battle::Unit & unit)
{
	const int perHit = newHorizonsHeroes::hardenedHitPermille(hero);
	if(perHit <= 0 || !unit.alive() || unit.isGhost())
		return std::nullopt;
	const int total = std::min(newHorizonsHeroes::hardenedTotalPermille(hero), hardenedPermille(unit) + perHit);
	Bonus marker(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, BonusType::NONE, BonusSource::HERO_SPECIAL,
		total, BonusSourceID(hero->getHeroTypeID()));
	marker.stacking = HARDENED_STACKING;
	marker.description.appendTextID("new-horizons.academy.hardened.effect");
	return marker;
}

bool isHardenedMarker(const Bonus * bonus)
{
	return bonus && bonus->type == BonusType::NONE && bonus->stacking == HARDENED_STACKING;
}

int hardenedPermille(const battle::Unit & unit)
{
	if(!unit.mayHaveBonusType(BonusType::NONE))
		return 0;
	static const CSelector selector = Selector::type()(BonusType::NONE).And(CSelector(isHardenedMarker));
	const auto held = unit.getBonuses(selector, "new-horizons:academy-specialty:hardened");
	int result = 0;
	for(const auto & bonus : *held)
		result = std::max(result, bonus->val);
	return std::clamp(result, 0, 750);
}

int serpentsEmbraceBoundPercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!attack.attacker || !attack.defender)
		return 0;
	const auto side = attack.attacker->unitSide();
	const int percent = side == BattleSide::ATTACKER || side == BattleSide::DEFENDER
		? newHorizonsHeroes::serpentsEmbraceBoundPercent(battle.battleGetFightingHero(side)) : 0;
	if(percent <= 0 || !attack.defender->hasBonusOfType(BonusType::BIND_EFFECT))
		return 0;
	// Only the restraint this Naga stack holds counts, not another source's hold.
	const auto held = newHorizonsSerpentsEmbrace::releasePlan(*attack.defender, attack.attacker->unitId());
	return held.empty() ? 0 : percent;
}

double attackDamageMultiplier(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!attack.attacker || !attack.defender)
		return 1.0;
	double result = 1.0;
	if(const int hardened = hardenedPermille(*attack.defender))
		result *= 1.0 - hardened / 1000.0;
	if(const int bound = serpentsEmbraceBoundPercent(battle, attack))
		result *= 1.0 + bound / 100.0;
	return result;
}
}
