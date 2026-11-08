/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SpellTargetsEvaluator.h"
#include "HypotheticBattle.h"
#include "AttackPossibility.h"
#include "RunicReflectionValue.h"
#include "../../lib/spells/effects/native/Damage.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
using namespace spells;
namespace
{
bool canonicalFate(const Mechanics * m)
{
	return m && m->getSpell()->getJsonKey() == "new-horizons:handOfFate" && m->battle()
		&& m->battle()->getBattle() && newHorizonsMagic::rulesActive(m->battle()->getBattle()->getMagicRules())
		&& newHorizonsMagic::spellAllowedBySavedRoster(m->battle()->getBattle()->getMagicRules(), m->getSpellId());
}
BattleStackAttacked injury(const Mechanics & m, uint32_t id, int64_t damage)
{
	BattleStackAttacked hit; hit.stackAttacked = id; hit.damageAmount = damage;
	hit.spellID = m.getSpellId(); hit.flags = BattleStackAttacked::SPELL_EFFECT; return hit;
}
float resolvedFateOutcome(const heroSpellProjection::OutcomeValue & value, const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & base, const std::shared_ptr<battle::CUnitState> & primary,
	const std::shared_ptr<battle::CUnitState> & collateral, std::vector<BattleStackAttacked> injuries, const heroSpellProjection::OutcomeKernel & kernel)
{
	auto board = std::make_shared<HypotheticBattle>(environment, base);
	board->updateUnit(primary->unitId(), primary->save(), -injuries.front().damageAmount);
	if(collateral) board->updateUnit(collateral->unitId(), collateral->save(), -injuries.back().damageAmount);
	for(const auto & hit : injuries)
	{
		const auto * target = board->battleGetUnitByID(hit.stackAttacked);
		const auto thaw = newHorizonsFrozen::removalPlan(*target);
		if(!thaw.empty()) board->removeUnitBonus(hit.stackAttacked, thaw);
	}
	value(board, injuries, false, true);
	return heroSpellProjection::localReceiptMarginal(board, kernel);
}
}
std::optional<SpellTargetEvaluator::HandOfFateExpectedDamageValue>
SpellTargetEvaluator::handOfFateExpectedDamageValue(const Mechanics * spellMechanics,
	const Target & target, PlayerColor scoringPlayer, std::shared_ptr<CBattleInfoCallback> battleState, const Environment * environment,
	const heroSpellProjection::OutcomeValue & outcomeValue, const std::shared_ptr<HypotheticBattle> & runicBoard)
{
	if(!canonicalFate(spellMechanics) || target.size() != 1 || !target.front().unitValue
		|| spellMechanics->getTargetTypes() != std::vector<AimType>{AimType::CREATURE})
		return std::nullopt;

	const auto * battle = spellMechanics->battle();
	const auto casterSide = spellMechanics->getCasterSide();
	if(!battle || (casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER))
		return std::nullopt;

	if(!battleState)
		battleState = std::shared_ptr<CBattleInfoCallback>(
			const_cast<CBattleInfoCallback *>(battle), [](CBattleInfoCallback *) {});

	const auto * primary = target.front().unitValue;
	if(primary->unitSide() != battle->otherSide(casterSide)
		|| !primary->alive() || !primary->isValidTarget(false))
		return std::nullopt;

	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(target, problem))
		return std::nullopt;

	// Spell resistance is resolved once for the selected target by the regular
	// cast path.  Forecast its probability without drawing from the live RNG.
	const float primaryApplicationChance = applicationChance(spellMechanics, primary);
	const auto primaryDamage = std::max<int64_t>(0, spellMechanics->adjustEffectValue(primary));
	auto projectedPrimary = primary->acquireState();
	const auto primaryHealthBefore = projectedPrimary->getAvailableHealth();
	auto primaryDamageToApply = primaryDamage;
	projectedPrimary->damage(primaryDamageToApply, false, battle::DamageProvenance::SPELL);
	const auto actualPrimaryDamage = std::max<int64_t>(0,
		primaryHealthBefore - projectedPrimary->getAvailableHealth());

	DamageCache damageCache;
	HandOfFateExpectedDamageValue result;
	const auto kernel = [&](const std::shared_ptr<HypotheticBattle> & board)
	{
		DamageCache local; float value = 0.0f;
		for(const auto * unit : board->battleGetAllUnits(false))
			if(unit && unit->alive() && !unit->isGhost() && !unit->isTurret())
			{
				const auto potential = AttackPossibility::calculateDamageReduce(nullptr, unit,
					unit->getAvailableHealth(), local, board);
				value += board->battleGetOwner(unit) == scoringPlayer ? potential : -potential;
			}
		return value;
	};
	auto coreBoard = outcomeValue ? std::make_shared<HypotheticBattle>(environment, battleState) : nullptr;
	const float coreOnly = outcomeValue ? outcomeValue(coreBoard, {}, false, false) : 0.0f;
	const auto localCore = outcomeValue ? heroSpellProjection::localReceiptMarginal(coreBoard, kernel) : 0.0f;
	const auto primaryHit = injury(*spellMechanics, primary->unitId(), actualPrimaryDamage);
	const float primaryOnly = outcomeValue && primaryApplicationChance > 0 ? resolvedFateOutcome(outcomeValue, environment, battleState,
		projectedPrimary, {}, {primaryHit}, kernel) : localCore;
	result.inspirationValue = coreOnly + primaryApplicationChance * (primaryOnly - localCore);
	if(actualPrimaryDamage > 0 && primaryApplicationChance > 0.0f)
	{
		result.hostileDamageValue += AttackPossibility::calculateDamageReduce(nullptr, primary,
			static_cast<uint64_t>(actualPrimaryDamage), damageCache, battleState) * primaryApplicationChance;
	}

	// Every living, on-field, non-turret stack remains in the draw pool,
	// including stacks that will reject the secondary packet (for example, a
	// Time Stopped or immune stack).  Rejection contributes zero value but never
	// changes the probability of another recipient being chosen.
	std::vector<const battle::Unit *> collateralPool;
	for(const auto * unit : battle->battleGetAllUnits(false))
	{
		if(!unit || unit == primary || !unit->alive() || !unit->getPosition().isValid() || unit->isTurret())
			continue;
		collateralPool.push_back(unit);
	}

	const auto spillRawDamage = actualPrimaryDamage / 2;
	if(collateralPool.empty() || spillRawDamage <= 0 || primaryApplicationChance <= 0.0f)
		return result;
	const auto * hero = spellMechanics->getHeroCaster();
	const bool fateDealer = spellMechanics->usesNewHorizonsMagicV3() && hero && hero->hasActivePerk(
		"new-horizons:chaosMagic", "new-horizons:chaosMagic.fateDealer");
	const auto hostileToCaster = [&](const battle::Unit * recipient)
	{
		return spellMechanics->battle()->battleGetOwner(recipient) != spellMechanics->getCasterColor();
	};
	const auto hostileCount = std::ranges::count_if(collateralPool, [&](const battle::Unit * recipient)
	{
		return hostileToCaster(recipient);
	});
	const auto friendlyCount = collateralPool.size() - hostileCount;
	const double poolSize = static_cast<double>(collateralPool.size());

	float expectedHostileSpillValue = 0.0f;
	float expectedFriendlySpillValue = 0.0f;
	const auto * nativeDamage = spellMechanics->findEffect<spells::effects::native::Damage>();
	for(const auto * recipient : collateralPool)
	{
		// This check is deliberately after pool construction: an invalid or
		// unreceptive recipient absorbs its share of the random selection chance.
		if(!recipient->isValidTarget(false) || recipient->isInvincible()
			|| recipient->hasImmunity(spellMechanics->getSpellId())
			|| recipient->hasAbsoluteImmunity(spellMechanics->getSpellId())
			|| !spellMechanics->isReceptive(recipient))
			continue;

		const auto adjustedDamage = std::max<int64_t>(0,
			spellMechanics->adjustRecipientDamage(recipient, spillRawDamage));

		auto projectedRecipient = recipient->acquireState();
		const auto recipientHealthBefore = projectedRecipient->getAvailableHealth();
		auto recipientDamageToApply = adjustedDamage;
		projectedRecipient->damage(recipientDamageToApply, false, battle::DamageProvenance::SPELL);
		const auto actualRecipientDamage = std::max<int64_t>(0,
			recipientHealthBefore - projectedRecipient->getAvailableHealth());

		const float recipientValue = AttackPossibility::calculateDamageReduce(nullptr, recipient,
			static_cast<uint64_t>(actualRecipientDamage), damageCache, battleState)
			* applicationChance(spellMechanics, recipient);
		// Selection is relative to the caster, scoring relative to the evaluating
		// player. They need not be the same perspective on a controlled stack.
		const double probability = fateDealer
			? static_cast<double>(hostileToCaster(recipient)
				? hostileCount + 2 * friendlyCount : friendlyCount) / (poolSize * poolSize)
			: 1.0 / poolSize;
		if(nativeDamage && runicBoard)
			result.runicSpillValue += static_cast<float>(primaryApplicationChance * probability)
				* runicReflectionAI::expectedRawDamageShare(*spellMechanics, *nativeDamage, *recipient,
					spillRawDamage, scoringPlayer, runicBoard, {1, 1, 1, false});
		if(outcomeValue)
		{
			const auto collateral = injury(*spellMechanics, recipient->unitId(), actualRecipientDamage);
			const auto full = resolvedFateOutcome(outcomeValue, environment, battleState,
				projectedPrimary, projectedRecipient, {primaryHit, collateral}, kernel);
			result.inspirationValue += primaryApplicationChance * probability
				* applicationChance(spellMechanics, recipient) * (full - primaryOnly);
		}
		if(battle->battleGetOwner(recipient) == scoringPlayer)
			expectedFriendlySpillValue += recipientValue * probability;
		else
			expectedHostileSpillValue += recipientValue * probability;
	}

	result.hostileDamageValue += expectedHostileSpillValue * primaryApplicationChance;
	result.friendlyDamageValue += expectedFriendlySpillValue * primaryApplicationChance;
	return result;
}
