/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsFactionSpellDamage.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "NewHorizonsShroud.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include "../mapObjects/CGHeroInstance.h"

namespace newHorizonsFactionSpellDamage
{
namespace
{
constexpr std::string_view EXPOSED_ATTACKER = "new-horizons:shroudOfMalassa.exposedWeakness.attacker.v1";
constexpr std::string_view EXPOSED_DEFENDER = "new-horizons:shroudOfMalassa.exposedWeakness.defender.v1";
BonusSourceID shroudSource()
{
	return BonusSourceID(SecondarySkill(SecondarySkill::decode(std::string(newHorizonsShroud::SKILL_ID))));
}
std::string_view exposedKey(BattleSide side)
{
	if(side == BattleSide::ATTACKER) return EXPOSED_ATTACKER;
	if(side == BattleSide::DEFENDER) return EXPOSED_DEFENDER;
	throw std::invalid_argument("Exposed Weakness requires a fighting hero side");
}
bool active(const CGHeroInstance * hero, std::string_view skill, std::string_view perk)
{
	return hero && hero->hasActivePerk(std::string(skill), std::string(perk));
}
}

Bonus exposedWeaknessMarker(BattleSide side)
{
	Bonus marker(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, BonusType::NONE,
		BonusSource::SECONDARY_SKILL, 20, shroudSource());
	marker.stacking = std::string(exposedKey(side));
	marker.description.appendRawString("Exposed Weakness: +20% damage from the flanking hero's spells until the next Creature Activation");
	return marker;
}
bool isExposedWeakness(const Bonus * bonus, BattleSide side)
{
	return bonus && (side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		&& bonus->type == BonusType::NONE && bonus->source == BonusSource::SECONDARY_SKILL
		&& bonus->sid == shroudSource() && bonus->duration == BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION
		&& bonus->val == 20 && bonus->stacking == exposedKey(side) && !bonus->parameters;
}
newHorizonsOrdinaryStrike::EffectPlan exposedWeakness(
	const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!battle.battleIsShroudFlankingAttack(attack)
		|| !active(battle.battleGetOwnerHero(attack.attacker), newHorizonsShroud::SKILL_ID,
			"new-horizons:shroudOfMalassa.exposedWeakness"))
		return {};
	const auto side = battle.playerToSide(battle.battleGetOwner(attack.attacker));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) return {};
	newHorizonsOrdinaryStrike::EffectPlan result;
	result.target = attack.defender->unitId();
	const auto existing = attack.defender->getBonusesOfType(BonusType::NONE);
	for(const auto & bonus : *existing)
		if(isExposedWeakness(bonus.get(), side)) result.remove.push_back(*bonus);
	result.add.push_back(exposedWeaknessMarker(side));
	return result;
}
int bonusPercent(const CBattleInfoCallback & battle, const CGHeroInstance * hero,
	const battle::Unit * recipient)
{
	if(!hero || !recipient || !recipient->alive() || recipient->isGhost()) return 0;
	int result = 0;
	// Conflux cast-time bonuses are prepared in the accepted CastProfile.
	// No death-output roster scan belongs in per-recipient damage evaluation.
	if(battle.battleGetOwner(recipient) == hero->getOwner()) return result;
	const auto side = battle.playerToSide(hero->getOwner());
	if(recipient->hasBonus(CSelector([side](const Bonus * bonus) { return isExposedWeakness(bonus, side); })))
		result += 20;
	if(active(hero, newHorizonsShroud::SKILL_ID, "new-horizons:shroudOfMalassa.shadowHex"))
	{
		for(const auto * ally : battle.battleGetAllUnits())
		{
			if(battle.battleGetOwner(ally) != hero->getOwner()) continue;
			BattleAttackInfo attack(ally, recipient, 0, false);
			if(battle.battleIsShroudFlankingAttack(attack))
			{
				result += 15;
				break;
			}
		}
	}
	return result;
}
}
