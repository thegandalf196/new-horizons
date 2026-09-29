/*
 * NewHorizonsPlague.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "NewHorizonsPlague.h"

#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "Unit.h"
#include "BattleHex.h"
#include "../CStack.h"
#include "../spells/BattleSpellMechanics.h"
#include "../spells/CSpell.h"
#include "../spells/ISpellMechanics.h"
#include "../spells/NewHorizonsMagic.h"

namespace
{
SpellID plagueSpellId()
{
	static const SpellID value(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID)));
	return value;
}

const CSpell * plagueSpell()
{
	return plagueSpellId().toSpell();
}
}

namespace newHorizonsPlague
{
bool hasPlague(const battle::Unit * unit)
{
	if(!unit)
		return false;

	const auto plagueMarker = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(plagueSpellId()))
		.And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER));
	return unit->hasBonus(plagueMarker);
}

std::optional<uint32_t> selectNextSpreadTarget(const CBattleInfoCallback & battle,
	const battle::Unit * afflicted, const std::function<bool(const battle::Unit *)> & recipientAllowed)
{
	if(!afflicted)
		return std::nullopt;

	// Order by the lowest adjacent hex occupied by each candidate, then unit ID.
	// This makes double-wide stacks and overlapping adjacency deterministic as well.
	std::map<uint32_t, int16_t> candidateHexes;
	const auto & surrounding = afflicted->getSurroundingHexes();
	std::vector<BattleHex> adjacentHexes(surrounding.begin(), surrounding.end());
	std::sort(adjacentHexes.begin(), adjacentHexes.end(), [](const BattleHex & lhs, const BattleHex & rhs)
	{
		return lhs.toInt() < rhs.toInt();
	});
	for(const auto & hex : adjacentHexes)
	{
		if(!hex.isValid())
			continue;
		const auto * candidate = battle.battleGetUnitByPos(hex, true);
		if(!candidate || !candidate->alive() || candidate == afflicted || hasPlague(candidate))
			continue;
		if(recipientAllowed && !recipientAllowed(candidate))
			continue;
		auto [found, inserted] = candidateHexes.emplace(candidate->unitId(), hex.toInt());
		if(!inserted)
			found->second = std::min(found->second, static_cast<int16_t>(hex.toInt()));
	}
	if(candidateHexes.empty())
		return std::nullopt;
	const auto first = std::min_element(candidateHexes.begin(), candidateHexes.end(),
		[](const auto & lhs, const auto & rhs)
		{
			return lhs.second != rhs.second ? lhs.second < rhs.second : lhs.first < rhs.first;
		});
	return first->first;
}

bool isSpreadRecipientReceptive(const CBattleInfoCallback & battle, BattleSide casterSide,
	const battle::Unit * recipient)
{
	if(!recipient || !recipient->alive() || !recipient->isValidTarget(false) || recipient->isInvincible())
		return false;

	const auto * spell = plagueSpell();
	const auto * caster = battle.battleGetFightingHero(casterSide);
	if(!spell || !caster)
		return false;

	spells::BattleCast cast(&battle, caster, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	return mechanics && mechanics->isReceptive(recipient);
}

int64_t rawTickDamage(const int32_t rawSpellPower, const int32_t coefficientBasisPoints)
{
	if(rawSpellPower < 0 || coefficientBasisPoints < 0 || coefficientBasisPoints > 100'000)
		throw std::invalid_argument("Invalid Plague damage formula inputs");
	// 25 + (0.8 x raw Spell Power x saved School/Spellcraft coefficient).
	// The power divisor is deliberately not used for this canonical formula.
	return 25 + static_cast<int64_t>(rawSpellPower) * 8 * coefficientBasisPoints
		/ (10 * SPELL_POWER_COEFFICIENT_BASIS_POINTS);
}

int64_t adjustedTickDamage(const CBattleInfoCallback & battle, BattleSide casterSide,
	const battle::Unit * target, const int64_t rawDamage)
{
	if(!target || rawDamage <= 0)
		return 0;
	const auto * spell = plagueSpell();
	if(!spell)
		return 0;

	const spells::Caster * caster = battle.battleGetFightingHero(casterSide);
	if(!caster)
	{
		for(const auto * unit : battle.battleGetAllStacks(true))
			if(unit && unit->unitSide() == casterSide)
			{
				caster = unit;
				break;
			}
	}
	if(!caster)
		return rawDamage;

	return spell->adjustRawDamage(caster, target, rawDamage, 0,
		battle.battleGetHoldTheLineMagicalReductionBasisPoints(target), 100, true,
		newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
			&& battle.getBattle()->getMagicRules()["rulesetVersion"].Integer()
				== newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION);
}
}
