/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsReactiveDefense.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsCombatSkills.h"
#include "Unit.h"
#include "../CCreatureHandler.h"
#include "../bonuses/BonusList.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../entities/hero/CHero.h"
#include "../mapObjects/CGHeroInstance.h"
#include <stdexcept>
namespace newHorizonsReactiveDefense
{
namespace
{
constexpr auto solidarity = "new-horizons:ordinary-creature-specialty:core:marius:demonicSolidarity";
constexpr auto ironHide = "new-horizons:ordinary-creature-specialty:core:alkin:ironHideSpent";
bool family(const battle::Unit & unit, const char * key)
{
	const auto base = CreatureID(CreatureID::decode(key));
	return unit.unitType() && (unit.creatureId() == base
		|| base.toCreature()->isMyDirectOrIndirectUpgrade(unit.unitType()));
}
bool plainStatus(const Bonus & bonus)
{
	return bonus.source == BonusSource::HERO_SPECIAL
		&& bonus.valType == BonusValueType::BASE_NUMBER
		&& bonus.targetSourceType == BonusSource::OTHER
		&& bonus.effectRange == BonusLimitEffect::NO_LIMIT && !bonus.appliedByEnemy
		&& bonus.bonusOwner == PlayerColor::CANNOT_DETERMINE
		&& bonus.spellCasterOwner == PlayerColor::CANNOT_DETERMINE
		&& bonus.statusTags.size() == 1 && bonus.statusTags[0] == BonusStatusTag::NON_TRANSFERABLE
		&& !bonus.parameters && !bonus.limiter && !bonus.propagator
		&& !bonus.updater && !bonus.propagationUpdater && bonus.customIconPath.empty();
}
}
int solidarityCapturedPercent(const CGHeroInstance * hero)
{
	if(!hero || !hero->getHeroType() || hero->getHeroType()->getJsonKey() != "core:marius"
		|| !newHorizonsHeroes::hasOrdinaryCreatureSpecialty(hero))
		return 0;
	return static_cast<int>(hero->getPrimaryGrowthRules()["additionalSpecialties"]["ordinaryCreatureSpecialties"]
		["demonicSolidarityReductionPercent"].Integer());
}
int solidarityConfiguredPercent(const CBattleInfoCallback & battle, BattleSide side)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return 0;
	const auto policy = battle.battleGetVisibleHeroPolicy(side);
	return solidarityCapturedPercent(policy.kind() == VisibleHeroPolicy::Knowledge::VISIBLE_HERO ? policy.legacyHero() : nullptr);
}
bool isSolidarityProtection(const Bonus * bonus)
{
	if(!bonus || bonus->stacking != solidarity)
		return false;
	if(!plainStatus(*bonus) || bonus->sid != BonusSourceID(HeroTypeID(HeroTypeID::decode("core:marius")))
		|| bonus->type != BonusType::GENERAL_DAMAGE_REDUCTION
		|| bonus->subtype != BonusCustomSubtype::damageTypeAll
		|| bonus->duration != BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION
		|| bonus->turnsRemain != 0 || bonus->val <= 0 || bonus->val > 100
		|| bonus->statusIdentity != "demonicSolidarity")
		throw std::runtime_error("Invalid Demonic Solidarity specialty provenance");
	return true;
}
bool isIronHideSpent(const Bonus * bonus)
{
	if(!bonus || bonus->stacking != ironHide)
		return false;
	if(!plainStatus(*bonus) || bonus->sid != BonusSourceID(HeroTypeID(HeroTypeID::decode("core:alkin")))
		|| bonus->type != BonusType::NONE || bonus->subtype != BonusSubtypeID()
		|| bonus->duration != BonusDuration::N_TURNS || bonus->turnsRemain < 0 || bonus->turnsRemain > 1
		|| bonus->val < 0 || bonus->statusIdentity != "ironHideSpent")
		throw std::runtime_error("Invalid Iron Hide specialty provenance");
	return true;
}
std::optional<Destruction> captureDestruction(const battle::Unit & unit)
{
	if(!newHorizonsCombatSkills::isOrdinaryCreatureAttacker(&unit))
		return std::nullopt;
	const auto side = unit.unitSide();
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return std::nullopt;
	return Destruction{unit.unitId(), side};
}
newHorizonsOrdinaryStrike::EffectPlan solidarityProtection(const battle::Unit & unit, int percent)
{
	newHorizonsOrdinaryStrike::EffectPlan effect;
	if(percent <= 0 || !unit.alive() || unit.isGhost() || unit.isTimeStopped()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(&unit) || !family(unit, "core:demon"))
		return effect;
	effect.target = unit.unitId();
	const auto protections = unit.getUnstackedBonuses(CSelector(isSolidarityProtection));
	for(const auto & existing : *protections)
	{
		effect.remove.push_back(*existing);
		percent = std::max(percent, existing->val);
	}
	Bonus guard(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, BonusType::GENERAL_DAMAGE_REDUCTION,
		BonusSource::HERO_SPECIAL, percent, BonusSourceID(HeroTypeID(HeroTypeID::decode("core:marius"))),
		BonusCustomSubtype::damageTypeAll, BonusValueType::BASE_NUMBER);
	guard.stacking = solidarity;
	guard.statusIdentity = "demonicSolidarity";
	guard.statusTags = {BonusStatusTag::NON_TRANSFERABLE};
	guard.description.appendRawString("Demonic Solidarity: " + std::to_string(percent)
		+ "% less physical damage until the next Creature Activation");
	effect.add.push_back(std::move(guard));
	return effect;
}
std::vector<newHorizonsOrdinaryStrike::EffectPlan> solidarityAftermath(
	const CBattleInfoCallback & battle, std::span<const Destruction> deaths)
{
	std::vector<newHorizonsOrdinaryStrike::EffectPlan> result;
	if(deaths.empty())
		return result;
	BattleSideArray<int> coefficients{};
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(std::ranges::any_of(deaths, [side](const Destruction & death) { return death.side == side; }))
			coefficients[side] = solidarityConfiguredPercent(battle, side);
	if(coefficients[BattleSide::ATTACKER] <= 0 && coefficients[BattleSide::DEFENDER] <= 0)
		return result;
	for(const auto * unit : battle.battleGetUnitsIf([](const battle::Unit * candidate)
		{ return candidate && candidate->alive() && !candidate->isGhost() && !candidate->isTimeStopped(); }))
	{
		const auto side = unit->unitSide();
		if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
			continue;
		if(!std::ranges::any_of(deaths, [&](const Destruction & death)
			{ return death.side == side && death.unit != unit->unitId(); }))
			continue;
		auto effect = solidarityProtection(*unit, coefficients[side]);
		if(!effect.add.empty())
			result.push_back(std::move(effect));
	}
	return result;
}
int ironHideConfiguredPercent(const CBattleInfoCallback & battle, const battle::Unit & unit)
{
	if(!family(unit, "core:gorgon") || !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(&unit))
		return 0;
	return newHorizonsHeroes::ordinaryCreatureSpecialtyValue(battle, unit, "core:alkin");
}
int ironHidePercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!attack.attacker || !attack.defender || !attack.physicalDamage || attack.shooting
		|| attack.secondaryAttack || attack.bracePreemptive || attack.preemptiveDamagePercent != 0
		|| attack.cleaveDamagePercent != 0 || !attack.attacker->alive() || !attack.defender->alive()
		|| attack.attacker->isGhost() || attack.defender->isGhost()
		|| attack.attacker->isTimeStopped() || attack.defender->isTimeStopped())
		return 0;
	const int percent = ironHideConfiguredPercent(battle, *attack.defender);
	if(percent <= 0 || !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attack.attacker)
		|| battle.battleGetOwner(attack.attacker) == battle.battleGetOwner(attack.defender))
		return 0;
	const auto spentMarkers = attack.defender->getUnstackedBonuses(CSelector(isIronHideSpent));
	for(const auto & marker : *spentMarkers)
		if(marker->val == battle.battleGetRound())
			return 0;
	return percent;
}
std::optional<Bonus> ironHideSpending(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(ironHidePercent(battle, attack) <= 0)
		return std::nullopt;
	Bonus marker(BonusDuration::N_TURNS, BonusType::NONE, BonusSource::HERO_SPECIAL,
		battle.battleGetRound(), BonusSourceID(HeroTypeID(HeroTypeID::decode("core:alkin"))),
		BonusSubtypeID(), BonusValueType::BASE_NUMBER);
	marker.turnsRemain = 1;
	marker.stacking = ironHide;
	marker.statusIdentity = "ironHideSpent";
	marker.statusTags = {BonusStatusTag::NON_TRANSFERABLE};
	marker.description.appendRawString("Iron Hide: protection already used in round "
		+ std::to_string(battle.battleGetRound()));
	return marker;
}
}
