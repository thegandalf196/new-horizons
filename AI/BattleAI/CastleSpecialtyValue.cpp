/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CastleSpecialtyValue.h"
#include "ConsecratedArmsValue.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/CastleSpecialtyPrayers.h"
#include "../../lib/battle/ConsecratedArmsChoice.h"
#include "../../lib/battle/ConsecratedArmsOrder.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include "../../lib/battle/NewHorizonsFrostMantle.h"
#include "AttackPossibility.h"
#include "../../lib/entities/hero/NewHorizonsCastleSpecialties.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"

namespace castleSpecialtyAI
{
namespace
{
constexpr float unevaluated = std::numeric_limits<float>::lowest() / 2;
/// Benediction pays out only with a later Order; credit half of its prayer now.
constexpr float BENEDICTION_DISCOUNT = 0.5f;

consecratedArms::Policy singlePrayerPolicy(const CGHeroInstance * hero)
{
	auto policy = consecratedArms::capturePolicy(hero);
	policy.perks &= ~static_cast<uint16_t>(consecratedArms::Perk::ANTIPHONAL_PRAYER);
	return policy;
}

float bestPrayerValue(const CGHeroInstance & hero, const std::shared_ptr<HypotheticBattle> & board, BattleSide side,
	const std::vector<uint32_t> & recipients, DamageCache & damage)
{
	const auto policy = singlePrayerPolicy(&hero);
	if(!policy.rank || !policy.chosen || recipients.empty())
		return 0.0f;
	const auto measured = consecratedArmsAI::measureRecipients(board, side, recipients, damage);
	const auto choice = consecratedArmsAI::bestChoice(policy, consecratedArms::actualFaith(hero), measured);
	return choice && choice->value > 0 ? choice->value : 0.0f;
}

std::optional<uint32_t> targetUnit(const PossibleSpellcast & candidate)
{
	if(candidate.dest.size() != 1 || !candidate.dest.front().unitValue)
		return std::nullopt;
	return candidate.dest.front().unitValue->unitId();
}
}

float sermonValue(const CPlayerBattleCallback & battle, const std::shared_ptr<HypotheticBattle> & board,
	BattleSide side, DamageCache & damage)
{
	const auto * hero = battle.battleGetFightingHero(side);
	if(!board || !newHorizonsHeroes::sermonRule(hero))
		return 0.0f;
	return bestPrayerValue(*hero, board, side, castleSpecialtyPrayers::sermonRecipients(battle, side), damage);
}

float benedictionValue(const CPlayerBattleCallback & battle, const std::shared_ptr<HypotheticBattle> & board,
	BattleSide side, uint32_t blessed, DamageCache & damage)
{
	const auto * hero = battle.battleGetFightingHero(side);
	if(!board || !newHorizonsHeroes::benedictionOfArmsRule(hero))
		return 0.0f;
	const auto holders = consecratedArms::prayerHolders(battle, side, {blessed});
	return BENEDICTION_DISCOUNT * bestPrayerValue(*hero, board, side, holders, damage);
}

float volleyValue(const CPlayerBattleCallback & battle, const std::shared_ptr<HypotheticBattle> & board,
	BattleSide side, uint32_t targetId, int focusFirePercent, DamageCache & damage)
{
	const int percent = newHorizonsHeroes::volleyCommandDamagePercent(battle.battleGetFightingHero(side));
	const auto * target = board ? board->battleGetUnitByID(targetId) : nullptr;
	if(percent <= 0 || !target || !target->alive())
		return 0.0f;
	// Issued before the shooters act, every ready shooter adds a shot now and
	// still takes its own turn; later in the round the volley is smaller.
	int64_t remaining = target->getAvailableHealth();
	double total = 0;
	for(const auto * unit : board->battleGetUnitsIf([side](const battle::Unit * candidate)
		{ return candidate->unitSide() == side && candidate->alive() && !candidate->isGhost(); }))
	{
		if(remaining <= 0 || !newHorizonsArchery::isOrdinaryPhysicalShooter(unit) || !unit->willMove(0)
			|| !board->battleCanShoot(unit, target->getPosition()))
			continue;
		const auto shot = damage.getDamage(unit, target, board) * percent * (100 + focusFirePercent) / 10000;
		total += std::min<int64_t>(remaining, shot);
		remaining -= shot;
	}
	return static_cast<float>(total);
}

bool keepsFrostMantleCentre(const spells::Mechanics & mechanics, BattleHex centre)
{
	return newHorizonsFrostMantle::centredOnStack(mechanics, battle::Target{battle::Destination(centre)});
}

float frostMantleValue(const CBattleInfoCallback & battle, const std::shared_ptr<HypotheticBattle> & board,
	BattleSide side, uint32_t centre, DamageCache & damage)
{
	const auto * hero = battle.battleGetFightingHero(side);
	const auto & rule = newHorizonsHeroes::frostMantleRule(hero);
	const auto * unit = battle.battleGetUnitByID(centre);
	if(!board || !rule || !unit || !unit->alive() || unit->unitSide() != side)
		return 0.0f;
	const auto measured = consecratedArmsAI::measureRecipients(board, side, {centre}, damage);
	if(measured.empty())
		return 0.0f;
	// Each melee hit returns part of its damage; the cloak lasts several rounds,
	// but only hits the nearest enemy is expected to deliver are counted.
	const auto & exposure = measured.front().exposure;
	const double returned = exposure.counterStrikes ? exposure.incoming : exposure.incoming * 0.5;
	return static_cast<float>(returned * newHorizonsHeroes::frostMantleReturnedPercent(hero) / 100.0);
}

float frostMantleChillValue(const CBattleInfoCallback & battle, const std::shared_ptr<HypotheticBattle> & board,
	BattleSide side, uint32_t centre, DamageCache & damage)
{
	const auto & rule = newHorizonsHeroes::frostMantleRule(battle.battleGetFightingHero(side));
	const auto * unit = board ? board->battleGetUnitByID(centre) : nullptr;
	if(!rule || !unit || !unit->alive() || unit->unitSide() == side)
		return 0.0f;
	// Slowing the stack delays the share of its strongest blow; tempo, not damage, so halve it.
	int64_t threat = 0;
	for(const auto * own : board->battleGetUnitsIf([side](const battle::Unit * candidate)
		{ return candidate->unitSide() == side && candidate->alive() && !candidate->isGhost(); }))
		threat = std::max(threat, damage.getDamage(unit, own, board));
	return static_cast<float>(threat * rule->initiativePenaltyPercent * rule->chillRounds / 200.0);
}

void addSpecialtyValue(std::vector<PossibleSpellcast> & candidates, const CPlayerBattleCallback & battle,
	const std::shared_ptr<HypotheticBattle> & board, BattleSide side, DamageCache & damage, float positive)
{
	const auto * hero = battle.battleGetFightingHero(side);
	if(!hero || !board)
		return;
	std::optional<float> sermon;
	for(auto & candidate : candidates)
	{
		if(candidate.value <= unevaluated)
			continue;
		if(candidate.command == HeroCommand::FOCUS_FIRE && candidate.focusFire)
		{
			const auto volley = volleyValue(battle, board, side, candidate.focusFire->targetUnitId,
				candidate.focusFire->rangedDamagePercent, damage);
			if(volley > 0)
				candidate.value += volley * positive;
			continue;
		}
		if(candidate.command != HeroCommand::NONE || !candidate.spell)
			continue;
		float bonus = 0.0f;
		auto action = BattleAction();
		action.actionType = EActionType::HERO_SPELL;
		action.side = side;
		action.spell = candidate.spell->id;
		if(castleSpecialtyPrayers::sermonCast(battle, action))
		{
			if(!sermon)
				sermon = sermonValue(battle, board, side, damage);
			bonus += *sermon;
		}
		if(candidate.spell->id == SpellID::BLESS)
			if(const auto blessed = targetUnit(candidate))
				bonus += benedictionValue(battle, board, side, *blessed, damage);
		if(candidate.spell->id == SpellID::FROST_RING && !candidate.dest.empty())
			if(const auto * centre = battle.battleGetUnitByPos(candidate.dest.front().hexValue, true))
				bonus += frostMantleValue(battle, board, side, centre->unitId(), damage)
					+ frostMantleChillValue(battle, board, side, centre->unitId(), damage);
		if(bonus > 0)
			candidate.value += bonus * positive;
	}
}
}
