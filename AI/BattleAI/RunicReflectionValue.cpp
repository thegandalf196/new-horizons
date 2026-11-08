/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "RunicReflectionValue.h"
#include "AttackPossibility.h"
#include "BattleEvaluator.h"
#include "CreatureActivationValue.h"
#include "MoraleActivationValue.h"
#include "MisfortuneValue.h"
#include "RunicControlValue.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsRunicReflection.h"
#include "../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsShadowMagnitudes.h"
#include "../../lib/spells/effects/Effect.h"
#include "../../lib/spells/effects/native/Damage.h"
#include "../../lib/spells/effects/native/Timed.h"
#include "../../lib/battle/ReachabilityInfo.h"
#include "../../lib/CCreatureHandler.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
namespace runicReflectionAI
{
namespace
{
using Kind = spells::effects::ResistanceShareKind;
struct Share { const spells::effects::Effect * effect; size_t index; int64_t rawDamage = -1; };
using Groups = std::map<uint32_t, std::vector<Share>>;
double resistance(const spells::Mechanics & mechanics, const battle::Unit & unit)
{
	return std::clamp(newHorizonsMetamagic::resistanceChance(unit.magicResistance(),
		mechanics.getMetamagicCastProfile(), *mechanics.battle(), unit), 0, 100) / 100.0;
}
bool reroll(const spells::Mechanics & mechanics, const battle::Unit & unit)
{
	const auto * board = mechanics.battle();
	const auto side = board->playerToSide(board->battleGetOwner(&unit));
	return (side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		&& side != mechanics.getEffectCasterSide() && board->getBattle()->getAdverseCombatRerollState(side).available();
}
std::optional<Groups> groups(const spells::Mechanics & mechanics, const spells::Target & aim)
{
	Groups result;
	bool unsupported = false;
	const auto canonical = mechanics.canonicalizeTarget(aim);
	mechanics.forEachEffect([&](const auto & effect)
	{
		if(effect.indirect) return false;
		// Do not certify mixed unknown bundles or run their scripted transform.
		if(effect.resistanceShareKind() == Kind::Unsupported) { unsupported = true; return true; }
		const auto targets = effect.transformTarget(&mechanics, aim, canonical);
		for(size_t index = 0; index < targets.size(); ++index)
		{
			const auto * unit = targets[index].unitValue;
			if(!unit || !unit->isValidTarget(false) || !mechanics.isReceptive(unit)
				|| mechanics.battle()->battleGetOwner(unit) == mechanics.getCasterColor()
				|| newHorizonsRunicReflection::nativeChance(*unit) == 0) continue;
			if(effect.resistanceShareKind() == Kind::Pandemonium && mechanics.getPandemoniumDebuffCount(unit) == 0) continue;
			auto & shares = result[unit->unitId()];
			if(std::ranges::none_of(shares, [&](const auto & share) { return share.effect == &effect; }))
				shares.push_back({&effect, index});
		}
		return false;
	});
	return unsupported ? std::nullopt : std::optional<Groups>(std::move(result));
}
float healthValue(const battle::Unit & before, const battle::CUnitState & after, PlayerColor reference,
	const std::shared_ptr<HypotheticBattle> & board, Weights weights)
{
	const auto delta = after.getAvailableHealth() - before.getAvailableHealth();
	if(delta == 0) return 0;
	const bool good = (board->battleGetOwner(&before) == reference) == (delta > 0);
	DamageCache cache;
	const auto magnitude = weights.healthUnits ? static_cast<float>(std::abs(delta))
		: AttackPossibility::calculateDamageReduce(nullptr, &before, std::abs(delta), cache, board);
	return good ? magnitude * weights.positive : -magnitude * weights.negative * weights.collateral;
}
float timedValue(const spells::Mechanics & reflected, const spells::effects::native::Timed & effect,
	const battle::Unit & recipient, PlayerColor reference, const std::shared_ptr<HypotheticBattle> & board,
	Weights weights, bool & unsupported)
{
	const auto incoming = effect.preparedRecipientBonuses(&reflected, &recipient);
	if(!incoming) { unsupported = true; return 0; }
	// Ownership transfer needs a signed control kernel, not ordinary stat pressure.
	if(std::ranges::any_of(*incoming, [](const auto & bonus) { return bonus.type == BonusType::HYPNOTIZED; }))
	{ unsupported = true; return 0; }
	const auto * exact = board->battleGetUnitByID(recipient.unitId());
	if(!exact) { unsupported = true; return 0; }
	StackWithBonuses projected(board.get(), exact);
	projected.updateUnitBonus(*incoming);
	if(projected.hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE))
		return berserkValue(*exact, projected, reference, board, weights);
	std::optional<ReachabilityInfo> distances;
	if(exact->getMovementRange() != projected.getMovementRange()) distances = board->getReachability(exact);
	DamageCache beforeCache, afterCache;
	const auto before = exact->canMove()
		? creatureActivationValue::expectedTargetActivationValue(exact, beforeCache, board, true,
			distances ? &*distances : nullptr, exact->getMovementRange()) : 0;
	const auto after = projected.canMove()
		? creatureActivationValue::expectedTargetActivationValue(&projected, afterCache, board, true,
			distances ? &*distances : nullptr, projected.getMovementRange()) : 0;
	float delta = after - before;
	int rounds = 0;
	bool morale = false, favorableChance = false, defense = false;
	for(const auto & bonus : *incoming)
	{
		if(Bonus::NTurns(&bonus)) rounds = std::max<int>(rounds, bonus.turnsRemain);
		morale |= bonus.type == BonusType::MORALE;
		favorableChance |= bonus.type == BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS;
		defense |= (bonus.type == BonusType::PRIMARY_SKILL && bonus.subtype == BonusSubtypeID(PrimarySkill::DEFENSE))
			|| bonus.type == BonusType::PHYSICAL_DEFENSE || bonus.type == BonusType::GENERAL_DAMAGE_REDUCTION
			|| bonus.type == BonusType::STACK_HEALTH;
	}
	if(favorableChance)
		delta -= expectedMisfortuneDeathBlowTargetActivationValue(exact, &projected, afterCache, board) * rounds * 0.5f;
	if(defense)
		delta -= BattleEvaluator::estimateProjectedFrailtyTargetValue(exact, &projected, afterCache, board, &beforeCache, true);
	if(morale && !exact->unaffectedByMorale() && !projected.unaffectedByMorale())
	{
		const auto side = board->playerToSide(board->battleGetOwner(exact));
		auto suppression = board->getMoraleSuppressionState(side);
		delta += moraleActivationValue::projectDelta(
			moraleActivationValue::expectedMoraleActivationChange(*board, exact),
			moraleActivationValue::expectedMoraleActivationChange(*board, &projected), rounds * 0.5f, suppression) * after;
	}
	if(exact->getInitiative() != projected.getInitiative() && projected.unitType())
		delta += static_cast<float>(projected.getCount()) * projected.unitType()->getAIValue()
			* static_cast<float>(projected.getInitiative() - exact->getInitiative())
			/ static_cast<float>(std::max(1, exact->getInitiative())) * 0.01f;
	const float signedDelta = board->battleGetOwner(exact) == reference ? delta : -delta;
	return signedDelta >= 0 ? signedDelta * weights.positive : signedDelta * weights.negative * weights.collateral;
}
float shareValue(const Share & share, const spells::Mechanics & reflected, const battle::Unit & recipient,
	const battle::Unit & reflector, PlayerColor reference, const std::shared_ptr<HypotheticBattle> & board,
	Weights weights, bool & unsupported)
{
	const auto kind = share.effect->resistanceShareKind();
	if(kind == Kind::Timed)
	{
		const auto * timed = dynamic_cast<const spells::effects::native::Timed *>(share.effect);
		if(!timed) { unsupported = true; return 0; }
		return timedValue(reflected, *timed, recipient, reference, board, weights, unsupported);
	}
	const auto * damage = dynamic_cast<const spells::effects::native::Damage *>(share.effect);
	if(!damage) return 0;
	int64_t amount = share.rawDamage >= 0 ? reflected.adjustRecipientDamage(&recipient, share.rawDamage)
		: kind == Kind::NaturesWrath ? reflected.getNaturesWrathDamage(&recipient, static_cast<int32_t>(share.index))
		: kind == Kind::Pandemonium ? reflected.getPandemoniumDamage(&recipient, reflected.getPandemoniumDebuffCount(&recipient))
		: kind == Kind::LifeDrain ? reflected.adjustEffectValue(&recipient)
		: damage->damageForTarget(share.index, &reflected, &recipient);
	auto target = recipient.acquireState();
	target->damage(amount, false, battle::DamageProvenance::SPELL);
	float value = healthValue(recipient, *target, reference, board, weights);
	if(kind == Kind::LifeDrain)
	{
		const auto lost = recipient.getAvailableHealth() - target->getAvailableHealth();
		const auto raw = static_cast<int64_t>(std::floor(static_cast<double>(lost)
			* newHorizonsMagic::lifeDrainHealingPercent(reflected.getHeroCaster()) / 100));
		auto healed = reflector.acquireState();
		auto healing = reflected.adjustMetamagicMagnitude(raw, &reflector);
		healed->heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT);
		value += healthValue(reflector, *healed, reference, board, weights);
	}
	return value;
}
Estimate expectedGroups(const spells::Mechanics & original, const Groups & affected,
	const spells::Mechanics & reflected, PlayerColor reference,
	const std::shared_ptr<HypotheticBattle> & board, Weights weights)
{
	Estimate result;
	if(affected.empty()) return result;
	for(const auto & [id, shares] : affected)
	{
		const auto * reflector = original.battle()->battleGetUnitByID(id);
		if(!reflector) continue;
		const auto r = resistance(original, *reflector);
		double chance = newHorizonsRunicReflection::reflectionChance(static_cast<int>(std::lround(r * 100)),
			newHorizonsRunicReflection::nativeChance(*reflector)) / 100.0;
		if(chance <= 0) continue;
		if(r > 0 && r < 1 && reroll(original, *reflector)) chance *= 2 - r;
		std::vector<const spells::effects::Effect *> effects;
		for(const auto & share : shares) effects.push_back(share.effect);
		const auto pool = newHorizonsRunicReflection::legalRecipients(reflected, effects, reflector);
		for(const auto * recipient : pool)
		{
			auto application = 1 - resistance(reflected, *recipient);
			if(application > 0 && application < 1 && reroll(reflected, *recipient)) application *= application;
			for(const auto & share : shares)
				result.value += static_cast<float>(chance * application / pool.size())
					* shareValue(share, reflected, *recipient, *reflector, reference, board, weights, result.partial);
		}
	}
	return result;
}
}
Estimate expectedValue(const spells::Mechanics & original, const spells::Target & aim,
	PlayerColor reference, const std::shared_ptr<HypotheticBattle> & board, Weights weights)
{
	if(!board || original.isMagicMirror() || !original.isMagicalEffect()) return {};
	if(std::ranges::none_of(board->battleGetAllUnits(false), [](const auto * unit)
		{ return unit && newHorizonsRunicReflection::nativeChance(*unit) > 0; })) return {};
	if(!original.isNegativeSpell())
	{
		bool wrath = false;
		original.forEachEffect([&](const auto & effect)
		{ wrath = effect.resistanceShareKind() == Kind::NaturesWrath; return wrath; });
		if(!wrath) return {};
	}
	const auto affected = groups(original, aim);
	// Native Unsupported bundles use ordinary resistance, with no reflection.
	// This is distinct from a reflectable share whose AI value is unavailable.
	if(!affected || affected->empty()) return {};
	const auto reflected = original.runicReflectionView();
	if(!reflected) return {0, true};
	return expectedGroups(original, *affected, *reflected, reference, board, weights);
}
float expectedRawDamageShare(const spells::Mechanics & original, const spells::effects::Effect & effect,
	const battle::Unit & reflector, int64_t raw, PlayerColor reference,
	const std::shared_ptr<HypotheticBattle> & board, Weights weights)
{
	if(!board || raw < 0 || original.isMagicMirror() || !original.isMagicalEffect() || !original.isNegativeSpell()
		|| effect.resistanceShareKind() != Kind::Damage || !reflector.isValidTarget(false)
		|| !original.isReceptive(&reflector) || newHorizonsRunicReflection::nativeChance(reflector) == 0
		|| original.battle()->battleGetOwner(&reflector) == original.getCasterColor()) return 0;
	const auto reflected = original.runicReflectionView();
	return reflected ? expectedGroups(original, Groups{{reflector.unitId(), {{&effect, 0, raw}}}},
		*reflected, reference, board, weights).value : 0;
}
}
