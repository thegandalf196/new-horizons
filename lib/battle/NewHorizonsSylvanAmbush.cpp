/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSylvanAmbush.h"
#include "NewHorizonsSylvanTactics.h"
#include "NewHorizonsSylvanTacticsV2.h"
#include "NewHorizonsRampartSpecialties.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "CUnitState.h"
#include "../CCreatureHandler.h"
#include "../combatScripts/IDamageCalculatorScript.h"
#include "../entities/hero/NewHorizonsPerkRules.h"
#include "../mapObjects/CGHeroInstance.h"
#include <limits>

namespace newHorizonsSylvanAmbush
{
namespace
{
constexpr std::string_view SKILL = "new-horizons:sylvanLuck";
using Source = battle::SylvanAmbushState::Source;

const battle::CUnitState * stateOf(const battle::Unit * unit)
{
	return dynamic_cast<const battle::CUnitState *>(unit);
}
const CGHeroInstance * heroOf(const CBattleInfoCallback & battle, const battle::Unit * unit)
{
	return newHorizonsSylvanTacticsV2::commandingHero(battle, unit);
}
BattleSide sideOf(const CBattleInfoCallback & battle, const battle::Unit * unit)
{
	return battle.playerToSide(battle.battleGetOwner(unit));
}
bool hostile(const CBattleInfoCallback & battle, const battle::Unit * first, const battle::Unit * second)
{
	return first && second && battle.battleGetOwner(first) != battle.battleGetOwner(second);
}
bool rampart(const battle::Unit * unit)
{
	return unit && unit->unitType() && unit->unitType()->getFactionID() == FactionID::RAMPART;
}
/// The two rounds in which a Hunting Pack mark still applies.
bool recent(int32_t marked, int32_t round)
{
	return marked >= 0 && (marked == round || marked == round - 1);
}
}

bool captured(const CGHeroInstance * hero)
{
	return hero && newHorizonsHeroes::usesSylvanTacticsV5(hero->getPerkState().getRules());
}
bool enabled(const CGHeroInstance * hero)
{
	return captured(hero) && hero->getPerkSkillRank(std::string(SKILL)) > 0;
}
bool hasPerk(const CGHeroInstance * hero, std::string_view perk)
{
	return enabled(hero) && hero->hasActivePerk(std::string(SKILL), std::string(SKILL) + "." + std::string(perk));
}
bool hasBattlePolicy(const CBattleInfoCallback & battle)
{
	return enabled(battle.battleGetFightingHero(BattleSide::ATTACKER))
		|| enabled(battle.battleGetFightingHero(BattleSide::DEFENDER));
}
int rankSlots(const CGHeroInstance * hero)
{
	return enabled(hero) ? std::clamp(hero->getPerkSkillRank(std::string(SKILL)), 0, 3) : 0;
}

bool concealed(const battle::Unit * unit)
{
	const auto * state = stateOf(unit);
	return state && state->alive() && !state->isGhost() && state->sylvanAmbush.concealed;
}
bool strikeReady(const battle::Unit * unit)
{
	const auto * state = stateOf(unit);
	return state && state->alive() && !state->isGhost() && state->sylvanAmbush.strikePending;
}
int footprintDistance(const battle::Unit & unit, BattleHex position, const battle::Unit & other, BattleHex otherPosition)
{
	int result = std::numeric_limits<int>::max();
	for(const auto & hex : unit.getHexes(position))
		for(const auto & otherHex : other.getHexes(otherPosition))
			if(hex.isValid() && otherHex.isValid())
				result = std::min<int>(result, BattleHex::getDistance(hex, otherHex));
	return result;
}
bool prey(const CBattleInfoCallback & battle, const battle::Unit * ambusher, const battle::Unit * target)
{
	const auto * victim = stateOf(target);
	const auto * hunter = stateOf(ambusher);
	if(!victim || !hunter || !hostile(battle, ambusher, target))
		return false;
	const auto & record = victim->sylvanAmbush;
	if(record.approachRound < 0 || record.approachRound != battle.battleGetRound())
		return false;
	const auto origin = hunter->sylvanAmbush.strikePending ? hunter->sylvanAmbush.strikeOrigin : hunter->getPosition();
	if(!origin.isValid())
		return false;
	return footprintDistance(*target, record.approachEnd, *ambusher, origin)
		< footprintDistance(*target, record.approachStart, *ambusher, origin);
}
bool ambushStrike(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!attack.attacker || !attack.defender || attack.retaliation || attack.secondaryAttack
		|| attack.bracePreemptive || attack.preemptiveDamagePercent > 0 || attack.cleaveDamagePercent > 0
		|| !attack.physicalDamage || !strikeReady(attack.attacker))
		return false;
	return newHorizonsSylvanTactics::ordinary(attack.attacker) && enabled(heroOf(battle, attack.attacker))
		&& prey(battle, attack.attacker, attack.defender);
}
bool startled(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!attack.retaliation || !attack.physicalDamage || attack.secondaryAttack || !attack.attacker || !attack.defender)
		return false;
	const auto * victim = stateOf(attack.attacker);
	const auto * hunter = stateOf(attack.defender);
	if(!victim || !hunter)
		return false;
	const bool marked = victim->sylvanAmbush.startledBy >= 0
		&& static_cast<uint32_t>(victim->sylvanAmbush.startledBy) == hunter->unitId();
	if(!marked && !hunter->sylvanAmbush.strikePending)
		return false;
	if(!hasPerk(heroOf(battle, hunter), "startle"))
		return false;
	// Forecasts estimate the retaliation before the strike marks its victim.
	return marked || (hostile(battle, hunter, victim) && newHorizonsSylvanTactics::ordinary(hunter)
		&& prey(battle, hunter, victim));
}

int waitStrikePercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack,
	bool patientHunter, int ordinaryPercent)
{
	const bool ambush = ambushStrike(battle, attack);
	int percent = 0;
	if(patientHunter && (ambush || attack.attacker->battlecraftWaitBonusAvailable()))
		percent += ambush ? PATIENT_HUNTER_AMBUSH_PERCENT : ordinaryPercent;
	if(ambush && hasPerk(heroOf(battle, attack.attacker), "apexPredator"))
		percent += APEX_PREDATOR_PERCENT;
	return percent;
}
int huntingPackPercent(const CBattleInfoCallback & battle, const battle::Unit * attacker,
	const battle::Unit * target)
{
	const auto * victim = stateOf(target);
	if(!victim || !recent(victim->sylvanAmbush.huntingPackRound, battle.battleGetRound())
		|| !victim->alive() || victim->isGhost() || !hostile(battle, attacker, target)
		|| !newHorizonsSylvanTactics::ordinary(attacker) || !rampart(attacker))
		return 0;
	const auto * hero = heroOf(battle, attacker);
	return hasPerk(hero, "elvenPrecision")
		? newHorizonsSylvanTacticsV2::perkValue(hero, "elvenPrecision", "attackDamagePercent") : 0;
}
void finalizeDamage(const CBattleInfoCallback & battle, const BattleAttackInfo & info, DamageAttackInfo & payload)
{
	if(startled(battle, info))
		payload.sylvanAttackMultipliers.push_back(STARTLE_RETALIATION_PERCENT);
}
bool dynamicDamage(const CBattleInfoCallback & battle, const battle::Unit * attacker, const battle::Unit * defender)
{
	// Exactly when an Ambush rider can apply (rule 4): a pending strike for
	// Patient Hunter and Apex Predator, a fresh Hunting Pack mark on the target,
	// or a retaliation against a pending or marked strike for Startle. A cached
	// pair is always computed without a rider, so it stays valid outside these.
	const auto * attackState = stateOf(attacker);
	const auto * defendState = stateOf(defender);
	if(!attackState || !defendState)
		return false;
	const auto * attackHero = heroOf(battle, attacker);
	if(attackState->sylvanAmbush.strikePending && enabled(attackHero)
		&& (hasPerk(attackHero, "serendipity") || hasPerk(attackHero, "apexPredator")))
		return true;
	if(recent(defendState->sylvanAmbush.huntingPackRound, battle.battleGetRound()) && hasPerk(attackHero, "elvenPrecision"))
		return true;
	const bool startleable = defendState->sylvanAmbush.strikePending
		|| (attackState->sylvanAmbush.startledBy >= 0
			&& static_cast<uint32_t>(attackState->sylvanAmbush.startledBy) == defendState->unitId());
	return startleable && hasPerk(heroOf(battle, defender), "startle");
}
ConcealmentOffer concealmentOffer(const CBattleInfoCallback & battle, const battle::CUnitState & unit)
{
	ConcealmentOffer offer;
	const auto & state = unit.sylvanAmbush;
	const auto round = battle.battleGetRound();
	if(state.concealed || state.harmedThisRound || state.concealedRound == round || state.concealedRound == round - 1
		|| unit.forestAmbush.concealed || !newHorizonsSylvanTactics::ordinary(&unit)
		|| unit.hasBonusOfType(BonusType::SANCTIFIED))
		return offer;
	const auto * hero = heroOf(battle, &unit);
	if(!enabled(hero))
		return offer;
	const auto side = sideOf(battle, &unit);
	int rankUsed = 0;
	bool coverUsed = false;
	// Counted, not stored: Waits resolve one at a time, so first come, first served.
	for(const auto * other : battle.battleGetUnitsIf([&](const battle::Unit * candidate)
		{ return candidate->unitId() != unit.unitId() && sideOf(battle, candidate) == side; }))
	{
		const auto * friendState = stateOf(other);
		if(!friendState || friendState->sylvanAmbush.concealedRound != round)
			continue;
		rankUsed += friendState->sylvanAmbush.source == Source::RANK ? 1 : 0;
		coverUsed |= friendState->sylvanAmbush.source == Source::FOREST_COVER;
	}
	offer.rankSlots = rankSlots(hero);
	offer.rankUsed = rankUsed;
	if(rankUsed < offer.rankSlots)
		offer.slot = Source::RANK;
	else if(!coverUsed && hasPerk(hero, "forestCover")
		&& newHorizonsRampart::besideObstacleOrWall(battle, unit, unit.getPosition()))
		offer.slot = Source::FOREST_COVER;
	return offer;
}
std::optional<Source> concealmentSlot(const CBattleInfoCallback & battle, const battle::CUnitState & unit)
{
	return concealmentOffer(battle, unit).slot;
}
void acceptWait(const CBattleInfoCallback & battle, battle::CUnitState & unit)
{
	if(const auto slot = concealmentSlot(battle, unit))
	{
		unit.sylvanAmbush.conceal(*slot, battle.battleGetRound());
		unit.sylvanAmbush.validateAt(battle.battleGetRound());
	}
}
void beginActivation(const CBattleInfoCallback & battle, battle::CUnitState & unit, bool delayed)
{
	auto & state = unit.sylvanAmbush;
	const bool ivorCandidate = delayed && unit.forestAmbush.concealed;
	if(!state.concealed && !state.strikePending && !ivorCandidate)
		return;
	// Ivor's Elves make Ambush strikes under the same prey and alternate-round rules.
	const bool ivor = ivorCandidate && newHorizonsSylvanTactics::ordinary(&unit) && enabled(heroOf(battle, &unit));
	state.beginActivation(delayed, battle.battleGetRound(), unit.getPosition(), ivor);
	state.validateAt(battle.battleGetRound());
}
void receiveAttack(const CBattleInfoCallback & battle, const BattleAttackInfo & attack, battle::CUnitState & target)
{
	auto & state = target.sylvanAmbush;
	if(!state.hasState() && !hasBattlePolicy(battle))
		return;
	state.harm();
	if(attack.secondaryAttack || !attack.defender || attack.defender->unitId() != target.unitId())
		return;
	if(ambushStrike(battle, attack))
	{
		state.startledBy = static_cast<int32_t>(attack.attacker->unitId());
		state.huntingPackRound = battle.battleGetRound();
	}
	else
		state.startledBy = -1;
}
void recordApproach(const CBattleInfoCallback & battle, battle::CUnitState & unit, BattleHex before, BattleHex after)
{
	if(before == after || (unit.sylvanAmbush.approachRound < 0 && !hasBattlePolicy(battle)))
		return;
	unit.sylvanAmbush.recordApproach(battle.battleGetRound(), before, after);
}
}
