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
#include "../GameLibrary.h"
#include "../constants/StringConstants.h"
#include "../modding/CModHandler.h"
#include "../modding/IdentifierStorage.h"
#include "../modding/ModScope.h"
#include "../spells/BattleSpellMechanics.h"
#include "../spells/CSpell.h"
#include "../spells/ISpellMechanics.h"
#include "../spells/NewHorizonsMagic.h"
#include "../spells/NewHorizonsSorcery.h"
#include "../spells/NewHorizonsOverwhelmingFormula.h"
#include "../networkPacks/SetStackEffect.h"
#include <vcmi/ServerCallback.h>
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

CSelector markerSelector(const JsonNode & savedRules)
{
	if(!LIBRARY || !LIBRARY->modh)
		throw std::runtime_error("Plague effect identity requires an initialized library");
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell", std::string(SPELL_ID), true);
	if(id)
		return Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(*id)))
			.And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER));
	const bool legacy = savedRules.isNull() || (savedRules.isStruct() && savedRules.Struct().empty());
	const bool moduleLoaded = vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE);
	if(!legacy || moduleLoaded)
		throw std::runtime_error("Plague effect identity is missing required New Horizons content");
	return Selector::none;
}

bool hasPlague(const battle::Unit * unit, const JsonNode & savedRules)
{
	return unit && unit->hasBonus(markerSelector(savedRules));
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
		if(!candidate || !candidate->alive() || candidate == afflicted || hasPlague(candidate, battle.getBattle()->getMagicRules()) || excluded.count(candidate->unitId()))
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
	const auto * battleInfo = battle.getBattle();
	if(!battleInfo || (casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER))
		return false;

	const auto * spell = plagueSpell();
	// Internal rule resolution needs the captured caster even when the public
	// player-scoped hero accessor deliberately hides the opposing side.
	const auto * caster = battleInfo->getSideHero(casterSide);
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

void completeActivation(const CBattleInfoCallback & battle, ServerCallback & server, uint32_t unitId)
{
	const auto * stack = battle.battleGetUnitByID(unitId);
	if(!stack || stack->isTimeStopped())
		return;
	const auto bonuses = stack->getBonuses(markerSelector(battle.getBattle()->getMagicRules()));
	const auto marker = bonuses->empty() ? std::shared_ptr<const Bonus>{} : bonuses->front();
	if(!marker)
		return; // Dispel or natural completion has removed the authoritative status.

	BattleSide casterSide = stack->unitSide();
	int32_t spreadAttempts = 0;
	int32_t lastProcessedRound = -1;
	int32_t sourceUnitId = -1;
	JsonNode parameters;
	if(marker->parameters)
	{
		try
		{
			parameters = marker->parameters->toCustom<JsonNode>();
			if(parameters.isStruct())
			{
				if(parameters["casterSide"].isNumber())
					casterSide = static_cast<BattleSide>(parameters["casterSide"].Integer());
				if(parameters["spreadAttempts"].isNumber())
					spreadAttempts = std::max<int32_t>(0, parameters["spreadAttempts"].Integer());
				if(parameters["lastProcessedRound"].isNumber())
					lastProcessedRound = parameters["lastProcessedRound"].Integer();
				if(parameters["sourceUnitId"].isNumber())
					sourceUnitId = parameters["sourceUnitId"].Integer();
			}
		}
		catch(const std::exception &)
		{
			// A legacy or malformed marker retains the safe defaults above.
		}
	}
	if(casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER)
		casterSide = stack->unitSide();
	const auto currentRound = battle.battleGetRound();
	if(lastProcessedRound >= currentRound)
		return; // Morale/Second Wind and other extra activations still tick once per round.

	const auto rawDamage = std::max<int64_t>(0, marker->val);
	const auto adjustedDamage = newHorizonsPlague::adjustedTickDamage(
		battle, casterSide, stack, rawDamage, &parameters["mdrPenetration"]);
	int64_t actualDamage = 0;
	if(stack->alive() && adjustedDamage > 0)
	{
		auto state = stack->acquireState();
		BattleStackAttacked hit;
		const auto * source = sourceUnitId >= 0 ? battle.battleGetUnitByID(static_cast<uint32_t>(sourceUnitId)) : nullptr;
		if(!source || source->unitSide() != casterSide)
			for(const auto * candidate : battle.battleGetAllUnits(true))
				if(candidate && candidate->unitSide() == casterSide)
				{
					source = candidate;
					break;
				}
		hit.attackerID = source ? source->unitId() : stack->unitId();
		hit.stackAttacked = stack->unitId();
		hit.damageAmount = adjustedDamage;
		hit.flags = BattleStackAttacked::SPELL_EFFECT;
		hit.spellID = plagueSpellId();
		CStack::prepareAttacked(hit, *server.getRNG(), state,
			false, false, battle::DamageProvenance::SPELL);
		actualDamage = hit.damageAmount;
		StacksInjured injury;
		injury.battleID = battle.getBattle()->getBattleID();
		injury.stacks.push_back(hit);
		if(auto claim = spells::overwhelmingFormulaClaim(battle, plagueSpellId().toSpell(),
			parameters["mdrPenetration"], injury.stacks))
			server.apply(*claim);
		server.apply(injury);
		// A detached callback may replace a borrowed source view with its own
		// post-hit state. Read alive/position/countdown from that current owner.
		stack = battle.battleGetUnitByID(unitId);
		if(!stack)
			return;
	}

	BattleLogMessage tickLog;
	tickLog.battleID = battle.getBattle()->getBattleID();
	MetaString tickLine;
	tickLine.appendRawString("Plague deals ");
	tickLine.appendNumber(actualDamage);
	tickLine.appendRawString(" magical damage to %s.");
	stack->addNameReplacement(tickLine, stack->getCount());
	tickLog.lines.push_back(std::move(tickLine));
	server.apply(tickLog);

	// Damage precedes each tick's captured propagation allowance. A lethal
	// final tick still propagates before its source status is discarded.
	const auto propagationLimit = newHorizonsPlague::capturedPropagationLimit(*marker);
	for(int32_t recipient = 0; recipient < propagationLimit; ++recipient)
	{
		const auto spreadTargetId = newHorizonsPlague::selectNextSpreadTarget(battle, stack,
			[&battle, casterSide](const battle::Unit * candidate)
			{
				return newHorizonsPlague::isSpreadRecipientReceptive(battle, casterSide, candidate);
			});
		if(!spreadTargetId) break;
		if(spreadTargetId)
		{
			const auto * spreadTarget = battle.battleGetUnitByID(*spreadTargetId);
			if(spreadTarget)
			{
				Bonus infection(*marker);
				infection.turnsRemain = 3;
				const auto infectionRound = battle.battleGetRound();
				if(infection.parameters)
				{
					try
					{
						JsonNode spreadParameters = infection.parameters->toCustom<JsonNode>();
						spreadParameters["spreadAttempts"].Integer() = 0;
						spreadParameters["lastProcessedRound"].Integer() = infectionRound - 1;
						infection.parameters = std::make_shared<BonusParameters>(spreadParameters);
					}
					catch(const std::exception &)
					{
						// Keep inherited parameters when loading a malformed legacy marker.
					}
				}
				SetStackEffect addInfection;
				addInfection.battleID = battle.getBattle()->getBattleID();
				addInfection.toAdd.emplace_back(spreadTarget->unitId(), std::vector<Bonus>{infection});
				server.apply(addInfection);
				if(!newHorizonsPlague::hasPlague(battle.battleGetUnitByID(*spreadTargetId), battle.getBattle()->getMagicRules()))
					break; // Advance only after the authoritative infection is present.

				BattleLogMessage spreadLog;
				spreadLog.battleID = battle.getBattle()->getBattleID();
				MetaString spreadLine;
				spreadLine.appendRawString(casterSide == spreadTarget->unitSide()
					? "Plague spreads to a friendly stack, %s."
					: "Plague spreads to an enemy stack, %s.");
				spreadTarget->addNameReplacement(spreadLine, spreadTarget->getCount());
				spreadLog.lines.push_back(std::move(spreadLine));
				server.apply(spreadLog);
			}
		}

	}
	SetStackEffect statusUpdate;
	statusUpdate.battleID = battle.getBattle()->getBattleID();
	const bool durationPaused = newHorizonsSorcery::durationPausedBySpellLock(*stack, *marker);
	if(stack->alive() && (marker->turnsRemain > 1 || (durationPaused && marker->turnsRemain > 0)))
	{
		Bonus nextStatus(*marker);
		nextStatus.turnsRemain = marker->turnsRemain - (durationPaused ? 0 : 1);
		JsonNode nextParameters;
		if(marker->parameters)
		{
			try
			{
				nextParameters = marker->parameters->toCustom<JsonNode>();
			}
			catch(const std::exception &)
			{
			}
		}
		nextParameters["casterSide"].Integer() = static_cast<int32_t>(casterSide);
		nextParameters["spreadAttempts"].Integer() = spreadAttempts < std::numeric_limits<int32_t>::max()
			? spreadAttempts + 1 : spreadAttempts;
		nextParameters["lastProcessedRound"].Integer() = currentRound;
		if(sourceUnitId >= 0)
			nextParameters["sourceUnitId"].Integer() = sourceUnitId;
		nextStatus.parameters = std::make_shared<BonusParameters>(nextParameters);
		// updateUnitBonus only extends turnsRemain and leaves parameters untouched.
		// Remove+add is therefore required to persist the round marker and countdown.
		statusUpdate.toRemove.emplace_back(stack->unitId(), std::vector<Bonus>{*marker});
		statusUpdate.toAdd.emplace_back(stack->unitId(), std::vector<Bonus>{std::move(nextStatus)});
	}
	else
		statusUpdate.toRemove.emplace_back(stack->unitId(), std::vector<Bonus>{*marker});
	server.apply(statusUpdate);
}

int64_t adjustedTickDamage(const CBattleInfoCallback & battle, BattleSide casterSide,
	const battle::Unit * target, const int64_t rawDamage, const JsonNode * capturedPenetration)
{
	if(!target || rawDamage <= 0)
		return 0;
	const auto * spell = plagueSpell();
	if(!spell)
		return 0;

	const auto * battleInfo = battle.getBattle();
	if(!battleInfo || (casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER))
		return rawDamage;
	const spells::Caster * caster = battleInfo->getSideHero(casterSide);
	if(!caster)
	{
		for(const auto * unit : battle.battleGetAllUnits(true))
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
