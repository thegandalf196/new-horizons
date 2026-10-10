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
#include "../bonuses/BonusParameters.h"
#include <cmath>
#include <limits>
#include "../spells/MagicalDamageReduction.h"

namespace
{
int32_t capturedIntegralLimit(const JsonNode & value)
{
	// LuaStack stores Lua-generated JSON numbers as DATA_FLOAT. Captured
	// markers accept only exactly integral, finite numbers within their range;
	// the authored raw-rule parser deliberately remains integer-only.
	if(!value.isNumber())
		throw std::runtime_error("Invalid captured Plague propagation limit");
	const auto number = value.Float();
	if(!std::isfinite(number) || number < 1 || number > std::numeric_limits<int32_t>::max()
		|| std::floor(number) != number)
		throw std::runtime_error("Invalid captured Plague propagation limit");
	return static_cast<int32_t>(number);
}

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
int32_t normalPropagationLimit(const JsonNode & rules)
{
	const auto & row = rules["spells"][std::string(SPELL_ID)];
	if(!row.isStruct() || !row.Struct().contains("propagationLimit"))
		return 1;
	const auto & value = row["propagationLimit"];
	if(value.getType() != JsonNode::JsonType::DATA_INTEGER || value.Integer() < 1
		|| value.Integer() >= std::numeric_limits<int32_t>::max())
		throw std::runtime_error("Invalid saved Plague propagation limit");
	return static_cast<int32_t>(value.Integer());
}

void validateRuleSerialization(const JsonNode & rules, bool supported)
{
	const auto & row = rules["spells"][std::string(SPELL_ID)];
	if(!row.isStruct() || !row.Struct().contains("propagationLimit"))
		return;
	if(!supported)
		throw std::runtime_error("Cannot discard saved Plague propagation rules");
	normalPropagationLimit(rules);
}

int32_t capturedPropagationLimit(const Bonus & marker)
{
	if(marker.type != BonusType::COMBAT_EVENT_TRIGGER || marker.source != BonusSource::SPELL_EFFECT
		|| marker.sid.toString() != SPELL_ID || !marker.parameters)
		return 1;
	const auto parameters = marker.parameters->toJsonNode();
	if(!parameters.isStruct() || !parameters.Struct().contains("propagationLimit"))
		return 1; // Legacy markers retain the old one-recipient rule.
	return capturedIntegralLimit(parameters["propagationLimit"]);
}

bool containsExtendedPropagation(const JsonNode & node)
{
	if(node.isStruct())
	{
		if(node["type"].isString() && node["type"].String() == "COMBAT_EVENT_TRIGGER"
			&& node["sourceType"].isString() && node["sourceType"].String() == "SPELL_EFFECT"
			&& node["sourceID"].isString() && node["sourceID"].String() == SPELL_ID)
		{
			const auto & parameters = node["addInfo"];
			if(parameters.isStruct() && parameters.Struct().contains("propagationLimit"))
			{
				if(capturedIntegralLimit(parameters["propagationLimit"]) > 1) return true;
			}
		}
		for(const auto & [key, child] : node.Struct())
			if(containsExtendedPropagation(child)) return true;
	}
	else if(node.isVector())
		for(const auto & child : node.Vector())
			if(containsExtendedPropagation(child)) return true;
	return false;
}

bool hasPlague(const battle::Unit * unit)
{
	if(!unit)
		return false;

	const auto plagueMarker = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(plagueSpellId()))
		.And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER));
	return unit->hasBonus(plagueMarker);
}

std::optional<uint32_t> selectNextSpreadTarget(const CBattleInfoCallback & battle,
	const battle::Unit * afflicted, const std::function<bool(const battle::Unit *)> & recipientAllowed,
	const std::set<uint32_t> & excluded)
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
		if(!candidate || !candidate->alive() || candidate == afflicted || hasPlague(candidate) || excluded.count(candidate->unitId()))
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
	const battle::Unit * target, const int64_t rawDamage, const JsonNode * capturedPenetration)
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

	const auto penetrations = capturedPenetration
		&& battle.battleGetOwner(target) != caster->getCasterOwner()
		? spells::capturedMdrPenetrations(*capturedPenetration, target->unitId(), battle.getBattle()) : std::vector<int>{};
	return spell->adjustRawDamage(caster, target, rawDamage, 0,
		battle.battleGetHoldTheLineMagicalReductionBasisPoints(target), 100, true,
		newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
			&& battle.getBattle()->getMagicRules()["rulesetVersion"].Integer()
				== newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION,
		true, battle.battleGetPerkMagicalReductionBasisPoints(target), penetrations);
}
}
