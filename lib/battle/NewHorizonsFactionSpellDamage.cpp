/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsFactionSpellDamage.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "ElementalRebirthLineage.h"
#include "NewHorizonsElementalRebirth.h"
#include "NewHorizonsShroud.h"
#include "NewHorizonsMagicalAbilityDamage.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include "../mapObjects/CGHeroInstance.h"
#include <limits>

namespace newHorizonsFactionSpellDamage
{
namespace
{
constexpr std::string_view REBIRTH = "new-horizons:elementalRebirth";
constexpr std::string_view OUTPUT = "new-horizons:elementalRebirth.output.v1";
constexpr std::string_view EXPOSED_ATTACKER = "new-horizons:shroudOfMalassa.exposedWeakness.attacker.v1";
constexpr std::string_view EXPOSED_DEFENDER = "new-horizons:shroudOfMalassa.exposedWeakness.defender.v1";
BonusSourceID rebirthSource()
{
	return BonusSourceID(SecondarySkill(SecondarySkill::decode(std::string(REBIRTH))));
}
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
Bonus rebirthOutputMarker()
{
	Bonus marker(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::OTHER, 0, rebirthSource());
	marker.stacking = std::string(OUTPUT);
	marker.hidden = true;
	return marker;
}
bool isRebirthOutputMarker(const Bonus * bonus)
{
	return bonus && bonus->type == BonusType::NONE && bonus->source == BonusSource::OTHER
		&& bonus->sid == rebirthSource() && bonus->duration == BonusDuration::ONE_BATTLE
		&& bonus->val == 0 && bonus->stacking == OUTPUT && bonus->hidden && !bonus->parameters;
}
bool isRebirthOutput(const battle::Unit & unit)
{
	if(!unit.unitType() || !unit.isSummoned() || unit.isClone() || unit.getPhantomInitialIntegrity() > 0)
		return false;
	// Historical first-generation outputs already have validated immutable provenance.
	if(newHorizonsElementalRebirth::isFirstGenerationOutput(unit)) return true;
	return unit.hasBonus(CSelector(isRebirthOutputMarker));
}
int livingRebirthCount(const CBattleInfoCallback & battle, const CGHeroInstance & hero)
{
	int count = 0;
	for(const auto * unit : battle.battleGetAllUnits())
		if(unit->alive() && !unit->isGhost() && battle.battleGetOwner(unit) == hero.getOwner()
			&& isRebirthOutput(*unit)) ++count;
	return count;
}
int bonusPercent(const CBattleInfoCallback & battle, const CGHeroInstance * hero,
	const battle::Unit * recipient)
{
	if(!hero || !recipient || !recipient->alive() || recipient->isGhost()) return 0;
	int result = 0;
	if(active(hero, REBIRTH, "new-horizons:elementalRebirth.elementalConduit"))
		result += 15 * livingRebirthCount(battle, *hero);
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
std::optional<Cataclysm> captureCataclysm(const CBattleInfoCallback & battle, const battle::Unit & unit)
{
	if(!unit.alive() || unit.isGhost() || !isRebirthOutput(unit)
		|| !active(battle.battleGetFightingHero(unit.unitSide()), REBIRTH,
			"new-horizons:elementalRebirth.elementalCataclysm")) return {};
	const auto maximum = battle::getMaximumHealth(unit);
	if(maximum <= 0) return {};
	return Cataclysm{unit.unitId(), unit.unitSide(), maximum,
		newHorizonsElementalRebirth::adjacentHostileUnitIds(battle, unit)};
}
int64_t cataclysmDamage(const CBattleInfoCallback & battle, const battle::Unit & target, int64_t rawShare)
{
	return target.isInvincible() ? 0
		: newHorizonsMagicalAbilityDamage::adjustDamage(battle, target, rawShare);
}
int64_t cataclysmShare(int64_t maximum, size_t targets)
{
	if(maximum <= 0 || targets == 0 || targets > static_cast<size_t>(std::numeric_limits<int64_t>::max())) return 0;
	return (maximum / 5) / static_cast<int64_t>(targets);
}
}
