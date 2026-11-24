/*
 * NewHorizonsWarcast.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsWarcast.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "NewHorizonsDivineMandate.h"
#include "NewHorizonsIronWill.h"
#include "Unit.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../spells/CSpellHandler.h"
#include "../spells/ISpellMechanics.h"

#include <vcmi/spells/Spell.h>

namespace newHorizonsWarcast
{
namespace
{
constexpr std::string_view EXPOSED_ATTACKER = "new-horizons:warcast.opening.exposed.attacker";
constexpr std::string_view EXPOSED_DEFENDER = "new-horizons:warcast.opening.exposed.defender";
constexpr std::string_view SUSTAINED_PREFIX = "new-horizons:warcast.sustained:";

std::string_view exposedKey(BattleSide side)
{
	if(side == BattleSide::ATTACKER) return EXPOSED_ATTACKER;
	if(side == BattleSide::DEFENDER) return EXPOSED_DEFENDER;
	throw std::invalid_argument("Opening requires a fighting hero side");
}

BonusSourceID skillSource()
{
	return BonusSourceID(SecondarySkill(SecondarySkill::decode(SKILL)));
}

/// A spell effect bonus cast by the side's own caster, not a creature on-hit ability.
const CSpell * ownSpell(const Bonus & bonus, PlayerColor owner)
{
	if(bonus.source != BonusSource::SPELL_EFFECT || bonus.spellCasterOwner != owner
		|| bonus.spellCasterOwner == PlayerColor::CANNOT_DETERMINE || bonus.abilityDescriptor)
		return nullptr;
	const auto spell = bonus.sid.as<SpellID>();
	return spell.hasValue() ? spell.toSpell() : nullptr;
}

std::vector<HeroOrderState> orderStates(const CBattleInfoCallback & battle, BattleSide side)
{
	if(const auto lease = battle.preparedHeroOrderStates(side))
		return *lease;
	return battle.battleGetHeroOrderStates(side);
}
}

int rankPercent(int rank)
{
	switch(rank)
	{
	case 1: return 20;
	case 2: return 35;
	case 3: return 50;
	default: return 0;
	}
}

int skillRank(const CGHeroInstance * hero)
{
	return hero ? std::clamp(hero->getPerkSkillRank(SKILL), 0, 3) : 0;
}

bool hasPerk(const CGHeroInstance * hero, const char * perk)
{
	return skillRank(hero) > 0 && hero->hasActivePerk(SKILL, perk);
}

int ownBeneficialSpellCount(const battle::Unit & unit, PlayerColor owner, int limit)
{
	std::vector<SpellID> seen;
	const auto effects = unit.getBonuses(Selector::sourceTypeSel(BonusSource::SPELL_EFFECT));
	for(const auto & effect : *effects)
	{
		const auto * spell = ownSpell(*effect, owner);
		if(!spell || !spell->isPositive() || std::find(seen.begin(), seen.end(), spell->getId()) != seen.end())
			continue;
		seen.push_back(spell->getId());
		if(static_cast<int>(seen.size()) >= limit)
			break;
	}
	return static_cast<int>(seen.size());
}

bool carriesOwnHostileSpell(const battle::Unit & unit, PlayerColor owner)
{
	const auto effects = unit.getBonuses(Selector::sourceTypeSel(BonusSource::SPELL_EFFECT));
	for(const auto & effect : *effects)
		if(const auto * spell = ownSpell(*effect, owner); spell && spell->isNegative())
			return true;
	return false;
}

void capture(const CBattleInfoCallback & battle, BattleSide side, HeroOrderState & order,
	const std::vector<uint32_t> & recipients)
{
	order.warcast.recipients.clear();
	order.warcast.riders = 0;
	const auto * hero = battle.battleGetFightingHero(side);
	const int base = rankPercent(skillRank(hero));
	if(base == 0 || recipients.empty())
		return;
	const auto owner = battle.sideToPlayer(side);
	const bool layered = hero->hasActivePerk(SKILL, LAYERED_ENCHANTMENT);
	const int grand = hero->hasActivePerk(SKILL, GRAND_CONVERGENCE) ? GRAND_BONUS_POINTS : 0;
	bool throughTarget = false;
	if(order.command == HeroCommand::FOCUS_FIRE)
		if(const auto * target = battle.battleGetUnitByID(order.primaryTargetUnitId))
			throughTarget = carriesOwnHostileSpell(*target, owner);
	for(const auto id : recipients)
	{
		const auto * unit = battle.battleGetUnitByID(id);
		if(!unit)
			continue;
		const int spells = ownBeneficialSpellCount(*unit, owner, layered ? 2 : 1);
		if(spells == 0 && !throughTarget)
			continue;
		const int percent = base + grand + (layered && spells >= 2 ? LAYERED_BONUS_POINTS : 0);
		order.warcast.recipients.push_back({id, static_cast<int16_t>(percent)});
	}
	if(order.warcast.recipients.empty())
		return;
	std::sort(order.warcast.recipients.begin(), order.warcast.recipients.end(),
		[](const auto & left, const auto & right) { return left.unitId < right.unitId; });
	const auto rider = [&](const char * perk, uint8_t bit)
	{
		if(hero->hasActivePerk(SKILL, perk))
			order.warcast.riders |= bit;
	};
	rider(KINDLED_EDGE, HeroOrderState::WARCAST_KINDLED);
	rider(OPENING, HeroOrderState::WARCAST_OPENING);
	rider(WARDED_RANKS, HeroOrderState::WARCAST_WARDED);
	rider(ENCHANTED_STEEL, HeroOrderState::WARCAST_STEEL);
}

int scaled(int value, int percent)
{
	if(percent <= 0)
		return value;
	return static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(value)
		+ static_cast<int64_t>(value) * percent / 100,
		std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
}

double scaled(double value, int percent)
{
	return percent <= 0 ? value : value * (100.0 + percent) / 100.0;
}

bool riderActive(const std::vector<HeroOrderState> & states, uint32_t unitId, int32_t round, uint8_t perk)
{
	for(const auto & state : states)
		if((state.warcast.riders & perk) && state.warcastPercentFor(unitId) > 0 && state.scheduledFor(unitId, round))
			return true;
	return false;
}

bool riderActive(const CBattleInfoCallback & battle, const battle::Unit * unit, uint8_t perk)
{
	if(!unit || !battle.getBattle())
		return false;
	// Cheap guards first: no stored Order state, or a side whose hero lacks the Skill.
	if(const auto presence = battle.getBattle()->heroOrderDamageStatePresence(); presence && !*presence)
		return false;
	if(skillRank(battle.battleGetOwnerHero(unit)) == 0)
		return false;
	const auto side = battle.playerToSide(battle.battleGetOwner(unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return false;
	return riderActive(orderStates(battle, side), unit->unitId(), battle.battleGetRound(), perk);
}

void addAttackRiders(const std::vector<HeroOrderState> & states, uint32_t attacker, int32_t round,
	int & orderDamagePercent, int & defenseIgnorePercent)
{
	if(states.empty())
		return;
	if(riderActive(states, attacker, round, HeroOrderState::WARCAST_KINDLED))
		orderDamagePercent += KINDLED_DAMAGE_PERCENT;
	if(riderActive(states, attacker, round, HeroOrderState::WARCAST_STEEL))
		defenseIgnorePercent += STEEL_DEFENSE_IGNORE_PERCENT;
}

bool carryAuthorized(const HeroOrderState & order, const CGHeroInstance * hero)
{
	if(!order.ironWillLifetime && !order.warcast.unbroken)
		return newHorizonsDivineMandate::hasDivineDisciplinePerk(hero);
	return (!order.ironWillLifetime || newHorizonsIronWill::hasPerk(hero))
		&& (!order.warcast.unbroken || hasPerk(hero, UNBROKEN_COMMAND));
}

int wardedRanksBasisPoints(const CBattleInfoCallback & battle, const battle::Unit * unit)
{
	return riderActive(battle, unit, HeroOrderState::WARCAST_WARDED) ? WARDED_REDUCTION_BASIS_POINTS : 0;
}

Bonus exposedMarker(BattleSide side)
{
	Bonus marker(BonusDuration::N_TURNS, BonusType::NONE, BonusSource::SECONDARY_SKILL,
		OPENING_SPELL_DAMAGE_PERCENT, skillSource());
	marker.turnsRemain = 2;
	marker.stacking = std::string(exposedKey(side));
	marker.description.appendRawString("Exposed: +20% damage from the opposing Warcasting hero's spells until the end of the next round");
	return marker;
}

bool isExposedMarker(const Bonus * bonus, BattleSide side)
{
	return bonus && (side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		&& isExposedMarker(bonus) && bonus->stacking == exposedKey(side);
}

bool isExposedMarker(const Bonus * bonus)
{
	return bonus && bonus->type == BonusType::NONE && bonus->source == BonusSource::SECONDARY_SKILL
		&& bonus->duration == BonusDuration::N_TURNS && bonus->val == OPENING_SPELL_DAMAGE_PERCENT
		&& (bonus->stacking == EXPOSED_ATTACKER || bonus->stacking == EXPOSED_DEFENDER) && !bonus->parameters;
}

int openingSpellDamagePercent(const CBattleInfoCallback & battle, const CGHeroInstance * hero,
	const battle::Unit * recipient)
{
	if(!recipient || !hasPerk(hero, OPENING) || battle.battleGetOwner(recipient) == hero->getOwner())
		return 0;
	const auto side = battle.playerToSide(hero->getOwner());
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return 0;
	return recipient->hasBonus(CSelector([side](const Bonus * bonus) { return isExposedMarker(bonus, side); }))
		? OPENING_SPELL_DAMAGE_PERCENT : 0;
}

int32_t fieldEnchanterCost(const spells::Mechanics & mechanics, const battle::Unit * target, int32_t cost)
{
	const auto * hero = mechanics.getHeroCaster();
	const auto * battle = mechanics.battle();
	if(cost <= 1 || !target || !battle || !mechanics.isPositiveSpell() || !hasPerk(hero, FIELD_ENCHANTER)
		|| battle->battleGetOwner(target) != hero->getOwner())
		return cost;
	const auto side = battle->playerToSide(hero->getOwner());
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return cost;
	const auto round = battle->battleGetRound();
	for(const auto & state : orderStates(*battle, side))
	{
		if(!state.scheduledFor(target->unitId(), round))
			continue;
		bool recipient = true;
		switch(state.command)
		{
		case HeroCommand::SECOND_WIND:
			recipient = state.secondWindActive && state.primaryTargetUnitId == target->unitId();
			break;
		case HeroCommand::FOCUS_FIRE:
			recipient = battle->battleIsFocusFireRecipient(target, side);
			break;
		case HeroCommand::HOLD_THE_LINE:
			recipient = state.isHoldTheLineRecipient(target->unitId(),
				static_cast<int16_t>(target->getPosition().toInt()), round);
			break;
		case HeroCommand::PROTECT:
		case HeroCommand::FLANK:
			recipient = false;
			break;
		default:
			break;
		}
		if(recipient)
			return std::max(1, cost - FIELD_ENCHANTER_MANA);
	}
	return cost;
}

Bonus sustainedMarker(const Bonus & effect)
{
	Bonus marker(BonusDuration::N_TURNS, BonusType::NONE, BonusSource::SECONDARY_SKILL, 0, skillSource());
	marker.turnsRemain = effect.turnsRemain;
	marker.stacking = std::string(SUSTAINED_PREFIX) + effect.sid.as<SpellID>().toSpell()->getJsonKey();
	return marker;
}

bool isSustainedMarker(const Bonus * bonus)
{
	return bonus && bonus->type == BonusType::NONE && bonus->source == BonusSource::SECONDARY_SKILL
		&& bonus->stacking.starts_with(SUSTAINED_PREFIX);
}
}
