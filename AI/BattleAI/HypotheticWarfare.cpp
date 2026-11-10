/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "HypotheticUnitEnvironment.h"
#include "../../lib/battle/NewHorizonsWarfare.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

void HypotheticBattle::captureWarfareStats(const CBattleInfoCallback & source)
{
	if(const auto * parent = dynamic_cast<const HypotheticBattle *>(&source))
	{
		orderBenefitStatValues = parent->orderBenefitStatValues;
		return;
	}
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * hero = source.battleGetFightingHero(side);
		const auto policy = newHorizonsWarfare::policy(hero);
		orderBenefitStatValues.at(side) = policy && hero->hasActivePerk(newHorizonsWarfare::SKILL,
			"new-horizons:command.veteranCommander") ? policy->veteranCommanderStats : 0;
	}
}

int HypotheticBattle::unitWarfareStatBonus(const battle::Unit * unit) const
{
	if(!unit) return 0;
	const auto side = playerToSide(battleGetOwner(unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) return 0;
	const int value = orderBenefitStatValues.at(side);
	return value && newHorizonsCommand::beneficiary(*this, unit) ? value : 0;
}

bool HypotheticUnitEnvironment::unitHasAmmoCart(const battle::Unit * u) const { return owner.unitHasAmmoCart(u); }
PlayerColor HypotheticUnitEnvironment::unitEffectiveOwner(const battle::Unit * u) const { return owner.unitEffectiveOwner(u); }
std::optional<int> HypotheticUnitEnvironment::unitMagicResistance(const battle::Unit * u) const { return owner.unitMagicResistance(u); }
int HypotheticUnitEnvironment::unitSylvanTacticsSpeed(const battle::Unit * u, int turn) const { return owner.unitSylvanTacticsSpeed(u, turn); }
int HypotheticUnitEnvironment::unitSylvanTacticsInitiativePercent(const battle::Unit * u) const { return owner.unitSylvanTacticsInitiativePercent(u); }
int HypotheticUnitEnvironment::unitFortuneSpeed(const battle::Unit * u) const { return owner.unitFortuneSpeed(u); }
int HypotheticUnitEnvironment::unitSpeedBonus(const battle::Unit * u) const { return owner.unitSpeedBonus(u); }
int HypotheticUnitEnvironment::unitWarfareStatBonus(const battle::Unit * u) const { return owner.unitWarfareStatBonus(u); }
bool HypotheticUnitEnvironment::unitSlowAffectsSpeed(const battle::Unit * u) const { return owner.unitSlowAffectsSpeed(u); }
newHorizonsNecromancy::CombatPassives HypotheticUnitEnvironment::unitNecromancyPassives(const battle::Unit * u) const { return owner.unitNecromancyPassives(u); }
newHorizonsElementalAttunement::UnitPassives HypotheticUnitEnvironment::unitElementalAttunement(const battle::Unit * u) const { return owner.unitElementalAttunement(u); }
int HypotheticUnitEnvironment::unitAdditionalRetaliations(const battle::Unit * u) const { return owner.unitAdditionalRetaliations(u); }
int HypotheticUnitEnvironment::unitBloodragePainIncrement(const battle::Unit * u) const { return owner.unitBloodragePainIncrement(u); }
bool HypotheticUnitEnvironment::unitHasVeteranCohesion(const battle::Unit * u) const { return owner.unitHasVeteranCohesion(u); }
std::optional<std::pair<int32_t, int32_t>> HypotheticUnitEnvironment::unitMoraleLimits(const battle::Unit * u) const { return owner.unitMoraleLimits(u); }
