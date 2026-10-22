/*
 * HypotheticCloneTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/CStack.h"
#include "../../lib/bonuses/BonusSelector.h"
#include <vcmi/Environment.h>
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <set>

namespace
{
class CloneEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit CloneEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

// Deliberately repeated packet IDs exercise the ordinary provider contract,
// not the separate InitialExport subject's mandatory unique-ID validation.
class ObservedRoster final : public CPlayerBattleCallback
{
	battle::Units roster;
public:
	mutable size_t calls = 0;
	mutable battle::Units visits;
	ObservedRoster(const IBattleInfo * source, PlayerColor player, battle::Units roster)
		: CPlayerBattleCallback(source, player), roster(std::move(roster)) {}
	battle::Units battleGetUnitsIf(const battle::UnitFilter & predicate) const override
	{
		++calls;
		battle::Units result;
		for(const auto * unit : roster)
		{
			visits.push_back(unit);
			if(predicate(unit))
				result.push_back(unit);
		}
		return result;
	}
	void resetObservations() const { calls = 0; visits.clear(); }
};

// Retain the original set-based membership algorithm as an ordered oracle.
battle::Units legacyEnumeration(const CBattleInfoCallback & source,
	const HypotheticBattle & branch, const battle::UnitFilter & predicate)
{
	const auto original = source.battleGetUnitsIf([](const battle::Unit *) { return true; });
	battle::Units result;
	std::set<uint32_t> originalIds;
	for(const auto * unit : original)
	{
		originalIds.insert(unit->unitId());
		const auto replacement = branch.stackStates.find(unit->unitId());
		const auto * current = replacement == branch.stackStates.end() ? unit : replacement->second.get();
		if(predicate(current))
			result.push_back(current);
	}
	for(const auto & [id, unit] : branch.stackStates)
		if(!originalIds.contains(id) && predicate(unit.get()))
			result.push_back(unit.get());
	return result;
}

void expectOrderedEnumeration(const CBattleInfoCallback & source,
	const HypotheticBattle & branch, const ObservedRoster & provider,
	const battle::UnitFilter & filter)
{
	provider.resetObservations();
	battle::Units expectedVisits;
	const auto expected = legacyEnumeration(source, branch, [&](const battle::Unit * unit)
	{
		expectedVisits.push_back(unit);
		return filter(unit);
	});
	const auto expectedProviderVisits = provider.visits;
	const auto expectedCalls = provider.calls;
	provider.resetObservations();
	battle::Units actualVisits;
	const auto actual = branch.getUnitsIf([&](const battle::Unit * unit)
	{
		actualVisits.push_back(unit);
		return filter(unit);
	});
	EXPECT_EQ(actual, expected);
	EXPECT_EQ(actualVisits, expectedVisits);
	EXPECT_EQ(provider.visits, expectedProviderVisits);
	EXPECT_EQ(provider.calls, expectedCalls);
}

auto branchUnitSnapshots(const HypotheticBattle & branch)
{
	std::map<uint32_t, std::pair<const StackWithBonuses *, JsonNode>> snapshots;
	for(const auto & [id, unit] : branch.stackStates)
		snapshots.emplace(id, std::pair(unit.get(), unit->save()));
	return snapshots;
}

auto liveUnitSnapshots(const CBattleInfoCallback & callback)
{
	std::vector<std::pair<const battle::Unit *, JsonNode>> snapshots;
	for(const auto * unit : callback.battleGetUnitsIf([](const battle::Unit *) { return true; }))
		snapshots.emplace_back(unit, unit->acquireState()->save());
	return snapshots;
}

template<typename Randomizer>
std::vector<std::byte> enumerationRandomizerBytes(Randomizer & randomizer)
{
	CMemorySerializer wire;
	wire.oser & randomizer;
	return wire.extractBuffer();
}
}

class HypotheticCloneTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		// Hypothetic Clone mechanics tests exercise core:clone independently of
		// the New Horizons roster, which intentionally disables that spell.
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
		// Preserve the effective captured hero rules, except the current-roster
		// starting-book table that cannot belong to this explicit legacy-magic fixture.
		JsonNode heroes = map->getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS);
		if(heroes.isStruct() && heroes["startingSkills"].isStruct()
			&& heroes["startingSkills"].Struct().contains("startingBookReplacements"))
		{
			heroes["startingSkills"].Struct().erase("startingBookReplacements");
			heroes.setOverrideFlag(true);
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, std::move(heroes));
		}
	}

	uint32_t originalId = 0;
	uint32_t cloneId = 0;
	std::shared_ptr<CloneEnvironment> environment;
	std::shared_ptr<CPlayerBattleCallback> callback;

	void prepareClone()
	{
		ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
		attackerSideHero->addSpellToSpellbook(SpellID::CLONE);
		const auto * spell = SpellID(SpellID::CLONE).toSpell();
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,
			3 * attackerSideHero->getEffectPowerDivisor(spell), ChangeValueMode::ABSOLUTE);
		const auto * original = battle()->battleGetStackByID(battle()->battleActiveUnit()->unitId());
		ASSERT_NE(original, nullptr);
		originalId = original->unitId();
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::CLONE;
		action.aimToUnit(original);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		ASSERT_TRUE(original->hasClone());
		cloneId = static_cast<uint32_t>(original->cloneID);
		ASSERT_TRUE(battle()->battleGetUnitByID(cloneId)->isClone());
		advanceRound();
		ASSERT_TRUE(battle()->battleGetUnitByID(cloneId)->alive());
		environment = std::make_shared<CloneEnvironment>(gameState());
		callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	}
};

TEST_F(HypotheticCloneTest, ChildAllocatorPreservesInheritedCloneAndGhostIdentities)
{
	ASSERT_NO_FATAL_FAILURE(prepareClone());
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	parent->removeUnit(cloneId);
	const auto * spell = SpellID(SpellID::CLONE).toSpell();
	spells::BattleCast cast(parent.get(), attackerSideHero, spells::Mode::HERO, spell);
	const battle::Target target{battle::Destination(parent->battleGetUnitByID(originalId))};
	ASSERT_TRUE(spell->battleMechanics(&cast)->canBeCastAt(target));
	cast.castEval(parent->getServerCallback(), target);
	const auto original = parent->getForUpdate(originalId);
	ASSERT_TRUE(original->hasClone());
	const auto projectedId = static_cast<uint32_t>(original->cloneID);
	ASSERT_TRUE(parent->battleGetUnitByID(projectedId)->alive());
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto allocated = child->nextUnitId();
	EXPECT_NE(allocated, projectedId);
	EXPECT_EQ(child->battleGetUnitByID(allocated), nullptr);
	child->removeUnit(projectedId);
	spells::BattleCast recast(child.get(), attackerSideHero, spells::Mode::HERO, spell);
	const battle::Target childTarget{battle::Destination(child->battleGetUnitByID(originalId))};
	ASSERT_TRUE(spell->battleMechanics(&recast)->canBeCastAt(childTarget));
	recast.castEval(child->getServerCallback(), childTarget);
	const auto replacementId = static_cast<uint32_t>(child->getForUpdate(originalId)->cloneID);
	EXPECT_NE(replacementId, projectedId);
	EXPECT_NE(replacementId, allocated);
	EXPECT_TRUE(child->battleGetUnitByID(projectedId)->isGhost());
	ASSERT_NE(child->battleGetUnitByID(replacementId), nullptr);
	EXPECT_TRUE(child->battleGetUnitByID(replacementId)->alive());
	HypotheticBattle grandchild(environment.get(), child);
	const auto next = grandchild.nextUnitId();
	EXPECT_NE(next, projectedId); // A retained ghost is still an occupied identity.
	EXPECT_NE(next, replacementId);
	EXPECT_EQ(grandchild.battleGetUnitByID(next), nullptr);
	EXPECT_TRUE(parent->battleGetUnitByID(projectedId)->alive());
	EXPECT_EQ(original->cloneID, static_cast<int32_t>(projectedId));
	EXPECT_TRUE(battle()->battleGetUnitByID(cloneId)->alive());
}

TEST_F(HypotheticCloneTest, TimedCloneExpiryReleasesOriginalBeforeNextNormalTurn)
{
	ASSERT_NO_FATAL_FAILURE(prepareClone());
	HypotheticBattle model(environment.get(), callback);
	const auto markerSelector = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::CLONE)));
	const auto markers = model.battleGetUnitByID(cloneId)->getBonuses(markerSelector);
	ASSERT_EQ(markers->size(), 1u);
	const int remaining = markers->front()->turnsRemain;
	ASSERT_GT(remaining, 0);
	for(int round = 1; round < remaining; ++round)
	{
		model.nextRound();
		EXPECT_TRUE(model.battleGetUnitByID(cloneId)->alive());
		EXPECT_TRUE(model.battleGetUnitByID(originalId)->hasClone());
	}
	model.nextRound();
	model.nextTurn(originalId, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(model.battleGetUnitByID(cloneId)->isGhost());
	EXPECT_FALSE(model.battleGetUnitByID(originalId)->hasClone());
	const auto * spell = SpellID(SpellID::CLONE).toSpell();
	spells::BattleCast cast(&model, attackerSideHero, spells::Mode::HERO, spell);
	EXPECT_TRUE(spell->battleMechanics(&cast)->canBeCastAt(
		battle::Target{battle::Destination(model.battleGetUnitByID(originalId))}));
	EXPECT_TRUE(battle()->battleGetUnitByID(originalId)->hasClone());
	EXPECT_TRUE(battle()->battleGetUnitByID(cloneId)->alive());
	EXPECT_EQ(battle()->battleGetUnitByID(cloneId)->getBonuses(markerSelector)->front()->turnsRemain, remaining);
}

TEST_F(HypotheticCloneTest, RemovingCloneReleasesOriginalForRecastWithoutChangingLiveBattle)
{
	ASSERT_NO_FATAL_FAILURE(prepareClone());
	HypotheticBattle model(environment.get(), callback);
	const auto * spell = SpellID(SpellID::CLONE).toSpell();
	spells::BattleCast cast(&model, attackerSideHero, spells::Mode::HERO, spell);
	EXPECT_FALSE(spell->battleMechanics(&cast)->canBeCastAt(
		battle::Target{battle::Destination(model.battleGetUnitByID(originalId))}));

	model.removeUnit(cloneId);
	EXPECT_TRUE(model.battleGetUnitByID(cloneId)->isGhost());
	EXPECT_FALSE(model.battleGetUnitByID(originalId)->hasClone());
	EXPECT_TRUE(spell->battleMechanics(&cast)->canBeCastAt(
		battle::Target{battle::Destination(model.battleGetUnitByID(originalId))}));
	EXPECT_TRUE(battle()->battleGetUnitByID(originalId)->hasClone());
	EXPECT_TRUE(battle()->battleGetUnitByID(cloneId)->alive());
	EXPECT_FALSE(battle()->battleGetUnitByID(cloneId)->isGhost());
}

TEST_F(HypotheticCloneTest, NestedCloneRemovalDoesNotClearParentOrLiveLinks)
{
	ASSERT_NO_FATAL_FAILURE(prepareClone());
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	parent->getForUpdate(originalId);
	HypotheticBattle child(environment.get(), parent);
	child.removeUnit(cloneId);
	EXPECT_FALSE(child.battleGetUnitByID(originalId)->hasClone());
	EXPECT_TRUE(parent->battleGetUnitByID(originalId)->hasClone());
	EXPECT_TRUE(parent->battleGetUnitByID(cloneId)->alive());
	EXPECT_TRUE(battle()->battleGetUnitByID(originalId)->hasClone());
	EXPECT_TRUE(battle()->battleGetUnitByID(cloneId)->alive());
}

TEST_F(HypotheticCloneTest, RemovingOriginalStillRemovesCloneAndRepeatedRemovalIsHarmless)
{
	ASSERT_NO_FATAL_FAILURE(prepareClone());
	HypotheticBattle model(environment.get(), callback);
	model.removeUnit(originalId);
	EXPECT_TRUE(model.battleGetUnitByID(originalId)->isGhost());
	EXPECT_TRUE(model.battleGetUnitByID(cloneId)->isGhost());
	EXPECT_FALSE(model.battleGetUnitByID(originalId)->hasClone());
	model.removeUnit(originalId);
	model.removeUnit(cloneId);
	EXPECT_TRUE(model.battleGetUnitByID(originalId)->isGhost());
	EXPECT_TRUE(model.battleGetUnitByID(cloneId)->isGhost());
	EXPECT_TRUE(battle()->battleGetUnitByID(originalId)->alive());
	EXPECT_TRUE(battle()->battleGetUnitByID(cloneId)->alive());
}

TEST_F(HypotheticCloneTest, OrdinaryEnumerationPreservesDuplicateProviderOverlayAndPredicateOrder)
{
	ASSERT_NO_FATAL_FAILURE(prepareClone());
	const auto liveBefore = gameState()->saveToMemory();
	const auto liveUnitsBefore = liveUnitSnapshots(*battle());
	const auto serverRandomBefore = enumerationRandomizerBytes(*gameHandler->randomizer);
	const auto aiRandomBefore = enumerationRandomizerBytes(CRandomGenerator::getDefault());
	auto roster = callback->battleGetUnitsIf([](const battle::Unit *) { return true; });
	const auto * repeated = battle()->battleGetUnitByID(originalId);
	ASSERT_NE(repeated, nullptr);
	roster.push_back(repeated);
	roster.insert(roster.begin(), repeated);
	auto provider = std::make_shared<ObservedRoster>(battle(), PlayerColor(0), std::move(roster));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), provider);
	ASSERT_FALSE(parent->initialExportSubject());
	expectOrderedEnumeration(*provider, *parent, *provider, [](const battle::Unit *) { return true; });
	expectOrderedEnumeration(*provider, *parent, *provider, [](const battle::Unit *) { return false; });
	parent->getForUpdate(originalId)->defending = true;
	parent->removeUnit(cloneId);
	ASSERT_TRUE(parent->battleGetUnitByID(cloneId)->isGhost());
	bool retainedCorpse = false;
	for(const auto * unit : battle()->battleAliveUnits())
		if(unit->unitId() != originalId && !unit->isClone() && !unit->isTurret()
			&& !unit->acquireState()->summoned)
		{
			auto corpse = parent->getForUpdate(unit->unitId());
			int64_t damage = corpse->getAvailableHealth();
			corpse->damage(damage);
			ASSERT_FALSE(corpse->alive());
			ASSERT_FALSE(corpse->isGhost());
			retainedCorpse = true;
			break;
		}
	ASSERT_TRUE(retainedCorpse);

	battle::UnitInfo addition;
	addition.id = parent->nextUnitId();
	addition.type = creatureByName("core:angel");
	addition.count = 3;
	addition.side = BattleSide::ATTACKER;
	addition.summoned = true;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(index);
		if(candidate.isAvailable() && !parent->battleGetUnitByPos(candidate, true))
		{
			addition.position = candidate;
			break;
		}
	}
	ASSERT_TRUE(addition.position.isAvailable());
	JsonNode data;
	addition.save(data);
	parent->addUnit(addition.id, data);
	ASSERT_NE(parent->battleGetUnitByID(addition.id), nullptr);
	EXPECT_TRUE(parent->battleGetUnitByID(addition.id)->acquireState()->summoned);
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	ASSERT_FALSE(child->initialExportSubject());
	child->getForUpdate(originalId)->defending = false;
	child->removeUnit(addition.id);
	ASSERT_TRUE(child->battleGetUnitByID(addition.id)->isGhost());
	const auto parentBefore = branchUnitSnapshots(*parent);
	const auto childBefore = branchUnitSnapshots(*child);

	const std::array<battle::UnitFilter, 4> filters{
		[](const battle::Unit *) { return true; },
		[](const battle::Unit *) { return false; },
		[](const battle::Unit * unit) { return unit->alive(); },
		[&](const battle::Unit * unit) { return unit->unitId() != originalId; }
	};
	for(int repetition = 0; repetition < 2; ++repetition)
		for(const auto & filter : filters)
		{
			expectOrderedEnumeration(*provider, *parent, *provider, filter);
			expectOrderedEnumeration(*parent, *child, *provider, filter);
		}
	const auto all = parent->getUnitsIf(filters.front());
	EXPECT_EQ(std::count(all.begin(), all.end(), parent->battleGetUnitByID(originalId)), 3);
	EXPECT_EQ(std::count(all.begin(), all.end(), parent->battleGetUnitByID(addition.id)), 1);
	EXPECT_TRUE(parent->battleGetUnitByID(originalId)->defended());
	EXPECT_FALSE(child->battleGetUnitByID(originalId)->defended());
	EXPECT_TRUE(parent->battleGetUnitByID(addition.id)->alive());
	EXPECT_EQ(branchUnitSnapshots(*parent), parentBefore);
	EXPECT_EQ(branchUnitSnapshots(*child), childBefore);
	EXPECT_EQ(enumerationRandomizerBytes(*gameHandler->randomizer), serverRandomBefore);
	EXPECT_EQ(enumerationRandomizerBytes(CRandomGenerator::getDefault()), aiRandomBefore);
	EXPECT_EQ(liveUnitSnapshots(*battle()), liveUnitsBefore);
	EXPECT_EQ(gameState()->saveToMemory(), liveBefore);
}
