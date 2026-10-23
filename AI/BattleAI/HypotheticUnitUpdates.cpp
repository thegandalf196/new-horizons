/*
 * HypotheticUnitUpdates.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/BattleEffectExchange.h"
#include "../../lib/battle/BattleForm.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/battle/NewHorizonsBloodrage.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/modding/ModScope.h"

namespace
{
std::optional<SpellID> entangleSpellId()
{
	if(!LIBRARY || !LIBRARY->identifiers())
		return std::nullopt;
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell", "new-horizons:entangle", true);
	if(!id || *id < 0)
		return std::nullopt;
	return SpellID(*id);
}

}

void HypotheticBattle::addUnit(uint32_t id, const JsonNode & data)
{
	invalidateLifeDrainFriendlyObservation(id);
	battle::UnitInfo info;
	info.load(id, data);
	auto newUnit = std::make_shared<StackWithBonuses>(this, info);
	stackStates[newUnit->unitId()] = newUnit;
	const auto orderState = getHeroOrderState(info.side, HeroCommand::RIPOSTE);
	const auto * hero = battleGetFightingHero(info.side);
	if(orderState && orderState->issuedRound == projectedRound
		&& hero && hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE)
		&& newUnit->alive() && !newUnit->isGhost() && !newUnit->isTurret()
		&& !newUnit->hasBonusOfType(BonusType::SIEGE_WEAPON)
		&& newUnit->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
		&& !newHorizonsOffense::hasVengeanceRetaliationBonus(newUnit.get()))
		addUnitBonus(newUnit->unitId(), {newHorizonsOffense::vengeanceRetaliationBonus()});
}

void HypotheticBattle::moveUnit(uint32_t id, const BattleHex & destination)
{
	std::shared_ptr<StackWithBonuses> changed = getForUpdate(id);
	const bool moved = changed->position != destination;
	if(moved && changed->hasBonusOfType(BonusType::BIND_EFFECT))
	{
		if(const auto entangleSpell = entangleSpellId())
			changed->removeUnitBonus(Selector::type()(BonusType::BIND_EFFECT)
				.And(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(*entangleSpell))));
	}
	changed->position = destination;
	if(!moved)
		return;

	const auto unitsAdjacent = [](const battle::Unit * first, const battle::Unit * second)
	{
		if(!first || !second)
			return false;
		for(const auto & firstHex : first->getHexes())
		{
			if(!firstHex.isValid())
				continue;
			for(const auto & secondHex : second->getHexes())
				if(secondHex.isValid() && BattleHex::getDistance(firstHex, secondHex) == 1)
					return true;
		}
		return false;
	};
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		if(auto state = getHeroOrderState(side, HeroCommand::HOLD_THE_LINE);
			state && !state->containsHoldBroken(id))
		{
			const auto * anchor = state->anchorFor(id);
			if(anchor && anchor->position != destination.toInt())
			{
				state->holdBrokenUnitIds.insert(std::lower_bound(
					state->holdBrokenUnitIds.begin(), state->holdBrokenUnitIds.end(), id), id);
				setHeroOrderState(side, state);
			}
		}

		if(auto state = getHeroOrderState(side, HeroCommand::PROTECT); state && !state->protectBroken)
		{
			const auto * protector = battleGetUnitByID(state->primaryTargetUnitId);
			const auto * ward = battleGetUnitByID(state->secondaryTargetUnitId);
			if(!protector || !ward || !protector->alive() || !ward->alive()
				|| !unitsAdjacent(protector, ward))
			{
				state->protectBroken = true;
				setHeroOrderState(side, state);
			}
		}
	}
}

std::vector<ProjectedOverwatchHit> HypotheticBattle::projectVoluntaryMovement(uint32_t id,
	const BattleHex & destination)
{
	std::vector<ProjectedOverwatchHit> result;
	auto mover = getForUpdate(id);
	if(!mover || !mover->alive() || destination == mover->getPosition() || !destination.isAvailable())
		return result;
	const auto start = mover->getPosition();
	auto path = mover->hasBonusOfType(BonusType::FLYING)
		? BattleHexArray{destination} : getPath(start, destination, mover.get()).first;
	// getPath stores destination first; traverse the same committed endpoints
	// as the authoritative voluntary mover, while flying only checks landing.
	for(size_t index = path.size(); index > 0 && mover->alive(); --index)
	{
		const auto from = mover->getPosition();
		const auto to = path[index - 1];
		moveUnit(id, to);
		for(const auto shooterId : battleGetOverwatchReactors(mover.get(), from, to))
		{
			auto shooter = getForUpdate(shooterId);
			if(!mover->alive() || !battleCanOverwatch(shooter.get(), mover.get(), from, to))
				continue;
			BattleAttackInfo attack(shooter.get(), mover.get(), 0, true);
			attack.retaliation = true;
			attack.archeryRangedDamageMultiplierPercent = newHorizonsBattlecraft::OVERWATCH_DAMAGE_PERCENT;
			int64_t damage = battleExpectedLuckDamage(attack);
			shooter->battlecraftOverwatchReadyRound = -1;
			shooter->battlecraftOverwatchUsedRound = getRound();
			shooter->afterAttack(true, false, true);
			shooter->removeUnitBonus(Bonus::UntilAttack);
			shooter->removeUnitBonus(Bonus::UntilOwnAttack);
			const auto source = captureElementalRebirthSource(*mover);
			const bool nativeRebirth = hasReadyNativeRebirth(mover.get());
			const auto healthBefore = mover->getAvailableHealth();
			mover->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
			const auto healthLoss = std::max<int64_t>(0, healthBefore - mover->getAvailableHealth());
			if(healthLoss > 0)
				mover->removeUnitBonus(Bonus::UntilBeingAttacked);
			result.push_back({shooterId, to, healthLoss});
			recordBloodrageTransition(mover, true);
			if(source)
				projectElementalRebirth(mover.get(), *source, !mover->alive(), mover->isClone(), nativeRebirth);
		}
	}
	return result;
}

void HypotheticBattle::updateUnit(uint32_t id, const JsonNode & data, int64_t healthDelta)
{
	std::shared_ptr<StackWithBonuses> changed = getForUpdateImpl(id);
	const bool wasAlive = changed->alive();
	const bool observing = lifeDrainFriendlyObservation && lifeDrainFriendlyObservation->id == id;
	std::optional<JsonNode> before;
	const auto revision = changed->getBattleFormViewRevision();
	const bool destroyedBefore = bloodrageDestroyedUnits.contains(id);
	if(observing)
	{
		auto & observation = *lifeDrainFriendlyObservation;
		observation.valid &= !observation.updateInProgress && healthDelta == 0 && wasAlive
			&& !changed->hasBattleFormState() && !changed->isBattleFormRestorationPending();
		observation.updateInProgress = true;
		try
		{
			before = changed->save();
		}
		catch(...)
		{
			observation.valid = false;
		}
	}

	changed->load(data);
	recordBloodrageTransition(changed, wasAlive);
	if(healthDelta < 0)
		changed->removeUnitBonus(Bonus::UntilBeingAttacked);

	if(observing)
	{
		auto & observation = *lifeDrainFriendlyObservation;
		observation.updateInProgress = false;
		try
		{
			observation.valid &= before && changed->alive() && !changed->isGhost()
				&& !changed->hasBattleFormState() && !changed->isBattleFormRestorationPending()
				&& revision == changed->getBattleFormViewRevision()
				&& destroyedBefore == bloodrageDestroyedUnits.contains(id) && *before == changed->save();
		}
		catch(...)
		{
			observation.valid = false;
		}
	}
}

void HypotheticBattle::removeUnit(uint32_t id)
{
	std::set<uint32_t> ids;
	ids.insert(id);

	while(!ids.empty())
	{
		auto toRemoveId = *ids.begin();

		auto toRemove = getForUpdate(toRemoveId);
		const bool wasAlive = toRemove->alive();

		if(!toRemove->ghost)
		{
			toRemove->onRemoved();

			//TODO: emulate detachFromAll() somehow

			//stack may be removed instantly (not being killed first)
			//handle clone remove also here
			if(toRemove->cloneID >= 0)
			{
				ids.insert(toRemove->cloneID);
				toRemove->cloneID = -1;
			}

			// Match BattleInfo::removeUnit: deleting a clone releases its original
			// for another Clone cast. Change only this model, including retained
			// dead/ghost descriptors and originals inherited from a parent model.
			const auto remaining = getUnitsIf([](const battle::Unit *) { return true; });
			for(const auto * unit : remaining)
			{
				auto linked = getForUpdate(unit->unitId());
				if(linked->cloneID >= 0 && static_cast<uint32_t>(linked->cloneID) == toRemoveId)
					linked->cloneID = -1;
			}
		}
		recordBloodrageTransition(toRemove, wasAlive);

		ids.erase(toRemoveId);
	}
}

void HypotheticBattle::recordBloodrageTransition(const std::shared_ptr<StackWithBonuses> & unit, bool wasAlive)
{
	if(bloodrageRanks[BattleSide::ATTACKER] == 0 && bloodrageRanks[BattleSide::DEFENDER] == 0)
		return;
	if(unit->alive())
	{
		bloodrageDestroyedUnits.erase(unit->unitId());
		return;
	}
	if(!wasAlive || unit->summoned || unit->isClone() || !bloodrageDestroyedUnits.insert(unit->unitId()).second)
		return;
	const auto category = getCreatureCategoryRules().lookup(unit->unitType()->getJsonKey());
	const bool eliteOrChampion = category
		&& category->category != newHorizonsCreatures::CreatureCategory::CORE;
	bool firstBloodSelected = false;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const bool firstBlood = newHorizonsBloodrage::hasFirstBlood(getSideHero(side));
		firstBloodSelected |= firstBlood;
		const int increments = newHorizonsBloodrage::deathIncrementCount(!bloodrageFirstBloodUsed,
			eliteOrChampion, firstBlood, newHorizonsBloodrage::hasSlayer(getSideHero(side)));
		bloodrageDamagePercents[side] = std::min(bloodrageCaps[side],
			bloodrageDamagePercents[side] + increments * newHorizonsBloodrage::incrementForRank(bloodrageRanks[side]));
	}
	bloodrageFirstBloodUsed |= firstBloodSelected;
}

battle::BattleEffectSnapshot HypotheticBattle::captureBattleEffects(uint32_t id) const
{
	const auto * unit = battleGetUnitByID(id);
	if(!unit || !unit->alive() || unit->isGhost() || unit->isTurret() || !unit->getPosition().isValid())
		throw std::runtime_error("Spell-effect exchange endpoint is not a living battlefield stack");
	battle::BattleEffectSnapshot result;
	if(const auto * projected = dynamic_cast<const StackWithBonuses *>(unit))
		result.effects = projected->localSpellEffects();
	else if(const auto * node = dynamic_cast<const CBonusSystemNode *>(unit))
	{
		for(const auto & bonus : node->getExportedBonusList())
			if(bonus->source == BonusSource::SPELL_EFFECT)
				result.effects.push_back(*bonus);
	}
	else
		throw std::runtime_error("Exact effect capture requires source-local bonus provenance");
	const auto * state = dynamic_cast<const battle::CUnitState *>(unit);
	if(!state)
		throw std::runtime_error("Exact effect exchange requires a typed unit state");
	result.sidecars = battle::captureBattleEffectSidecars(*state);
	result.recipientHealth = state->acquireState()->save()["state"]["health"];
	result.capacityHealthReferenceMax = state->getCapacityHealthReferenceMax();
	return result;
}

void HypotheticBattle::exchangeBattleEffects(const battle::BattleEffectExchange & exchange)
{
	for(const auto & endpoint : exchange.endpoints)
		invalidateLifeDrainFriendlyObservation(endpoint.id);
	exchange.validateShape();
	for(const auto & endpoint : exchange.endpoints)
		if(!battle::exactBattleEffectSnapshotEqual(captureBattleEffects(endpoint.id), endpoint.expected))
			throw std::runtime_error("Stale detached spell-effect exchange endpoint snapshot");
	auto prepared = stackStates;
	for(const auto & endpoint : exchange.endpoints)
	{
		const auto * original = battleGetUnitByID(endpoint.id);
		auto replacement = std::make_shared<StackWithBonuses>(this, original);
		// All mutations affect an unpublished clone. Its source bearer remains
		// alive through the existing projectedBearer ownership mechanism.
		auto health = battle::prepareBattleEffectHealth(*replacement, endpoint.expected, endpoint.replacement, getMagicRules());
		replacement->replaceLocalSpellEffectsExact(endpoint.replacement.effects);
		replacement->commitPreparedCapacityHealth(*health.unit);
		battle::commitBattleEffectSidecars(*replacement, endpoint.replacement.sidecars);
		prepared[endpoint.id] = std::move(replacement);
	}
	stackStates.swap(prepared);
	++bonusTreeVersion;
}

void HypotheticBattle::addUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
	getForUpdate(id)->addUnitBonus(bonus);
	bonusTreeVersion++;
}

void HypotheticBattle::updateUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
	getForUpdate(id)->updateUnitBonus(bonus);
	bonusTreeVersion++;
}

void HypotheticBattle::removeUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
	const auto unit = getForUpdate(id);
	const auto markers = unit->getBonuses(CSelector(battle::isPolymorphMarker));
	const bool removesForm = std::ranges::any_of(bonus, [&markers](const Bonus & entry)
	{
		return battle::isPolymorphMarker(&entry) && std::ranges::any_of(*markers, [&entry](const auto & installed)
		{
			return installed->sid == entry.sid && installed->val == entry.val;
		});
	});
	if(removesForm && unit->hasBattleForm()
		&& (unit->isTimeStopped()
			|| !battle::endBattleFormAtNearestLegalPosition(*unit, getAccessibility(unit.get()))))
	{
		auto removable = bonus;
		vstd::erase_if(removable, [](const Bonus & entry) { return battle::isPolymorphMarker(&entry); });
		unit->removeUnitBonus(removable);
	}
	else
		unit->removeUnitBonus(bonus);
	bonusTreeVersion++;
}

uint32_t HypotheticBattle::nextUnitId() const
{
	// Parent models can already own IDs in the projected allocation range.
	// Retained ghosts also reserve their identities (clone links/target cohorts).
	const auto maximum = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
	while(nextId <= maximum && battleGetUnitByID(nextId))
		++nextId;
	if(nextId > maximum)
		throw std::overflow_error("Hypothetical battle unit identities exhausted");
	return nextId++;
}
