/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSylvanTacticsRevision.h"
#include "NewHorizonsSylvanTactics.h"
#include "NewHorizonsSylvanTacticsV2.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../CCreatureHandler.h"
#include "../GameConstants.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include "../entities/hero/NewHorizonsPerkRules.h"
#include "../mapObjects/CGHeroInstance.h"
#include <stdexcept>

namespace newHorizonsSylvanTacticsRevision
{
namespace
{
constexpr std::string_view SKILL = "new-horizons:sylvanLuck";
constexpr auto DAPPLED_IDENTITY = "new-horizons:sylvanLuck.dappledCover.movement.v4";
constexpr auto REDEPLOYMENT_IDENTITY = "new-horizons:sylvanLuck.quickRedeployment.initiative.v4";
BonusSourceID skillSource()
{
	return BonusSourceID(SecondarySkill(SecondarySkill::decode(std::string(SKILL))));
}
Bonus marker(BonusType type, int value, const char * identity, const char * status)
{
	Bonus result(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, type, BonusSource::SECONDARY_SKILL,
		value, skillSource(), BonusSubtypeID(), BonusValueType::ADDITIVE_VALUE);
	result.stacking = identity;
	result.statusIdentity = status;
	result.statusTags = {BonusStatusTag::NON_TRANSFERABLE};
	return result;
}
bool matches(const Bonus * bonus, const char * identity, BonusType type, int minimum, int maximum)
{
	if(!bonus || bonus->stacking != identity)
		return false;
	if(bonus->type != type || bonus->subtype != BonusSubtypeID() || bonus->source != BonusSource::SECONDARY_SKILL
		|| bonus->sid != skillSource() || bonus->duration != BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION
		|| bonus->valType != BonusValueType::ADDITIVE_VALUE || bonus->val < minimum || bonus->val > maximum
		|| bonus->turnsRemain != 0 || bonus->effectRange != BonusLimitEffect::NO_LIMIT
		|| bonus->statusTags != std::vector{BonusStatusTag::NON_TRANSFERABLE}
		|| bonus->parameters || bonus->limiter || bonus->propagator || bonus->updater || bonus->propagationUpdater)
		throw std::runtime_error("Invalid Sylvan Tactics revision marker");
	return true;
}
bool rampart(const battle::Unit * unit)
{
	return unit && unit->unitType() && unit->unitType()->getFactionID() == FactionID::RAMPART;
}
}

bool revised(const CGHeroInstance * hero)
{
	return hero && newHorizonsHeroes::usesSylvanTacticsV4(hero->getPerkState().getRules());
}
bool active(const CGHeroInstance * hero, std::string_view perk)
{
	return revised(hero) && hero->hasActivePerk(std::string(SKILL), std::string(SKILL) + "." + std::string(perk));
}

bool isDappledMovement(const Bonus * bonus)
{
	return matches(bonus, DAPPLED_IDENTITY, BonusType::NONE, 1, GameConstants::BFIELD_SIZE);
}
newHorizonsOrdinaryStrike::EffectPlan dappledMovement(const CBattleInfoCallback & battle,
	const battle::Unit & unit, bool voluntary, int hexes)
{
	newHorizonsOrdinaryStrike::EffectPlan result;
	if(!voluntary || hexes <= 0 || !newHorizonsSylvanTactics::ordinary(&unit)
		|| !active(newHorizonsSylvanTacticsV2::commandingHero(battle, &unit), "luckyRecovery"))
		return result;
	int64_t total = hexes;
	for(const auto & existing : *unit.getUnstackedBonuses(CSelector(isDappledMovement)))
	{
		// Protection is already earned for this activation; nothing changes.
		if(existing->val >= DAPPLED_COVER_MINIMUM_HEXES)
			return {};
		total += existing->val;
		result.remove.push_back(*existing);
	}
	auto bonus = marker(BonusType::NONE, static_cast<int>(std::min<int64_t>(total, GameConstants::BFIELD_SIZE)),
		DAPPLED_IDENTITY, "sylvanDappledCover");
	result.target = unit.unitId();
	result.add.push_back(std::move(bonus));
	return result;
}
int dappledCoverPercent(const battle::Unit * unit)
{
	return unit && unit->hasBonus(CSelector([](const Bonus * bonus)
	{
		return isDappledMovement(bonus) && bonus->val >= DAPPLED_COVER_MINIMUM_HEXES;
	})) ? DAPPLED_COVER_REDUCTION_PERCENT : 0;
}

bool isQuickRedeployment(const Bonus * bonus)
{
	return matches(bonus, REDEPLOYMENT_IDENTITY, BonusType::STACKS_INITIATIVE_FLAT,
		QUICK_REDEPLOYMENT_INITIATIVE, QUICK_REDEPLOYMENT_INITIATIVE);
}
newHorizonsOrdinaryStrike::EffectPlan quickRedeployment(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack)
{
	newHorizonsOrdinaryStrike::EffectPlan result;
	// Only an ordinary retaliation qualifies: Brace, pre-emptive blows,
	// Counterfire and Overwatch reach this owner as secondary strikes.
	if(!attack.retaliation || attack.secondaryAttack || attack.bracePreemptive
		|| attack.preemptiveDamagePercent > 0 || attack.cleaveDamagePercent > 0
		|| !newHorizonsSylvanTactics::ordinary(attack.attacker)
		|| !active(newHorizonsSylvanTacticsV2::commandingHero(battle, attack.attacker), "natureSProvidence"))
		return result;
	result.target = attack.attacker->unitId();
	for(const auto & existing : *attack.attacker->getUnstackedBonuses(CSelector(isQuickRedeployment)))
		result.remove.push_back(*existing);
	auto bonus = marker(BonusType::STACKS_INITIATIVE_FLAT, QUICK_REDEPLOYMENT_INITIATIVE,
		REDEPLOYMENT_IDENTITY, "sylvanQuickRedeployment");
	bonus.description.appendRawString("Quick Redeployment: +2 Initiative until the next Creature Activation");
	result.add.push_back(std::move(bonus));
	return result;
}
int quickRedeploymentInitiative(const battle::Unit * unit)
{
	return unit && unit->hasBonus(CSelector(isQuickRedeployment)) ? QUICK_REDEPLOYMENT_INITIATIVE : 0;
}

bool huntingPackConfigured(const CBattleInfoCallback & battle, const battle::Unit * attacker)
{
	return newHorizonsSylvanTactics::ordinary(attacker) && rampart(attacker)
		&& active(newHorizonsSylvanTacticsV2::commandingHero(battle, attacker), "elvenPrecision");
}
int huntingPackPercent(const CBattleInfoCallback & battle, const battle::Unit * attacker,
	const battle::Unit * target)
{
	// Same predicate as Righteous Sentence: Waiting is not a completed action.
	if(!target || !target->alive() || target->isGhost() || target->hasCompletedActionThisRound()
		|| !huntingPackConfigured(battle, attacker) || battle.battleGetOwner(attacker) == battle.battleGetOwner(target))
		return 0;
	return newHorizonsSylvanTacticsV2::perkValue(newHorizonsSylvanTacticsV2::commandingHero(battle, attacker),
		"elvenPrecision", "attackDamagePercent");
}

std::vector<std::string> statusRows(const battle::Unit * unit)
{
	std::vector<std::string> result;
	if(!newHorizonsSylvanTactics::ordinary(unit))
		return result;
	if(const int percent = dappledCoverPercent(unit))
		result.push_back("Dappled Cover: " + std::to_string(percent)
			+ "% ranged creature-damage protection until this stack's next activation.");
	if(const int initiative = quickRedeploymentInitiative(unit))
		result.push_back("Quick Redeployment: +" + std::to_string(initiative)
			+ " Initiative until this stack's next activation.");
	return result;
}
}
