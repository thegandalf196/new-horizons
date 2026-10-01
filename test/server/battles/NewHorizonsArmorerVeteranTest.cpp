/*
 * NewHorizonsArmorerVeteranTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/CStack.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/BattleChanges.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"

namespace
{
constexpr auto armorerSkill = "new-horizons:armorer";
constexpr auto pavisePerk = "new-horizons:armorer.pavise";
constexpr auto veteranPerk = "new-horizons:armorer.veteran";
constexpr auto damageHistoryKey = "veteranPhysicalDamageSinceActivation";

class NewHorizonsArmorerVeteranTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void selectVeteran(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(armorerSkill);
		ASSERT_GE(decoded, 0);
		const auto armorer = SecondarySkill(decoded);
		hero->setSecSkillLevel(armorer, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({armorerSkill, pavisePerk});
		ASSERT_TRUE(hero->hasActivePerk(armorerSkill, pavisePerk));
		hero->setSecSkillLevel(armorer, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({armorerSkill, veteranPerk});
		ASSERT_TRUE(hero->hasActivePerk(armorerSkill, veteranPerk));
	}

	void removeStartingStacks()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	void applyDamage(CStack * stack, int64_t amount, battle::DamageProvenance provenance)
	{
		auto state = stack->acquireState();
		const auto healthBefore = state->getAvailableHealth();
		state->damage(amount, false, provenance);

		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = state->getAvailableHealth() - healthBefore;
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}

	std::size_t queueActivations(const CStack * stack) const
	{
		return static_cast<std::size_t>(std::ranges::count_if(server.stackActivations,
			[stack](const BattleSetActiveStack & activation)
		{
			return activation.stack == stack->unitId()
				&& activation.reason == BattleUnitTurnReason::TURN_QUEUE;
		}));
	}

	bool advanceUntilActivation(CStack * stack, std::size_t previousActivations)
	{
		for(int attempt = 0; attempt < 48; ++attempt)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			if(active->unitId() == stack->unitId() && queueActivations(stack) > previousActivations)
				return true;

			const auto player = battle()->sideToPlayer(active->unitSide());
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0), player,
				BattleAction::makeDefend(active)))
				return false;
		}
		return false;
	}
};
}

TEST_F(NewHorizonsArmorerVeteranTest, DamageHistoryDefaultsCopiesRoundTripsAndRejectsInvalidValues)
{
	startGame();
	startBattle();
	auto * stack = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(92), 10);
	ASSERT_NE(stack, nullptr);

	auto state = stack->acquireState();
	EXPECT_EQ(state->veteranPhysicalDamageSinceActivation, 0);
	auto saved = state->save();
	EXPECT_EQ(saved["state"][damageHistoryKey].Integer(), 0);

	state->veteranPhysicalDamageSinceActivation = 137;
	auto copied = stack->acquireState();
	*copied = *state;
	EXPECT_EQ(copied->veteranPhysicalDamageSinceActivation, 137);

	saved = state->save();
	auto restored = stack->acquireState();
	restored->load(saved);
	EXPECT_EQ(restored->veteranPhysicalDamageSinceActivation, 137);

	auto legacy = saved;
	legacy["state"].Struct().erase(damageHistoryKey);
	restored->load(legacy);
	EXPECT_EQ(restored->veteranPhysicalDamageSinceActivation, 0)
		<< "Older unit-state JSON defaults the new accumulator to zero";

	auto invalid = saved;
	invalid["state"][damageHistoryKey] = JsonNode(-1);
	EXPECT_THROW(restored->load(invalid), std::runtime_error);
}

TEST_F(NewHorizonsArmorerVeteranTest, TracksOnlyActualPhysicalCreatureHealthLossAndConsumesTheInterval)
{
	startGame();
	selectVeteran(defenderSideHero);
	startBattle();
	auto * stack = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(92), 2);
	ASSERT_NE(stack, nullptr);
	const auto initialHealth = stack->getAvailableHealth();

	auto state = stack->acquireState();
	state->guardianSpiritHitPoints = 25;
	state->guardianSpiritRoundsRemaining = 2;
	int64_t absorbed = 25;
	state->damage(absorbed, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(state->guardianSpiritHitPoints, 0);
	EXPECT_EQ(state->getAvailableHealth(), initialHealth);
	EXPECT_EQ(state->veteranPhysicalDamageSinceActivation, 0)
		<< "A hit absorbed entirely by Guardian Spirit is not creature HP loss";

	state->guardianSpiritHitPoints = 15;
	state->guardianSpiritRoundsRemaining = 2;
	int64_t physical = 100;
	state->damage(physical, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(state->getAvailableHealth(), initialHealth - 85);
	EXPECT_EQ(state->veteranPhysicalDamageSinceActivation, 85)
		<< "The accumulator contains only creature health lost after buffer absorption";

	int64_t spell = 20;
	state->damage(spell, false, battle::DamageProvenance::SPELL);
	EXPECT_EQ(state->getAvailableHealth(), initialHealth - 105);
	EXPECT_EQ(state->veteranPhysicalDamageSinceActivation, 85)
		<< "Spell damage does not enter the physical-damage interval";

	const auto healthBeforeRecovery = state->getAvailableHealth();
	EXPECT_EQ(newHorizonsCombatSkills::applyVeteran(state.get(), defenderSideHero), 12)
		<< "Recovery floors 15 percent of 85 physical HP to 12";
	EXPECT_EQ(state->getAvailableHealth(), healthBeforeRecovery + 12);
	EXPECT_EQ(state->veteranPhysicalDamageSinceActivation, 0);
	EXPECT_EQ(newHorizonsCombatSkills::applyVeteran(state.get(), defenderSideHero), 0)
		<< "A consumed interval cannot be recovered twice";

	int64_t unskilledPhysical = 100;
	state->damage(unskilledPhysical, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(newHorizonsCombatSkills::applyVeteran(state.get(), nullptr), 0);
	EXPECT_EQ(state->veteranPhysicalDamageSinceActivation, 0)
		<< "The interval is consumed when no owning hero can apply Veteran";
}

TEST_F(NewHorizonsArmorerVeteranTest, RecoveryCannotRestoreCasualties)
{
	startGame();
	selectVeteran(defenderSideHero);
	startBattle();
	auto * stack = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(92), 2);
	ASSERT_NE(stack, nullptr);
	auto state = stack->acquireState();
	int64_t damage = 225;
	state->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	const auto survivingCount = state->getCount();
	ASSERT_EQ(survivingCount, 1);
	ASSERT_TRUE(state->alive());

	EXPECT_EQ(newHorizonsCombatSkills::applyVeteran(state.get(), defenderSideHero), 25)
		<< "The 33 HP recovery request is limited to 25 missing HP on the surviving creature";
	EXPECT_EQ(state->getCount(), survivingCount)
		<< "Veteran healing does not resurrect casualties";
}

TEST_F(NewHorizonsArmorerVeteranTest, LiveActivationConsumesHistoryAndPublishesRecoveryLog)
{
	startGame();
	selectVeteran(defenderSideHero);
	startBattle();
	removeStartingStacks();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 10);
	auto * veteranStack = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(93), 10);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(veteranStack, nullptr);
	beginCombat();
	ASSERT_TRUE(advanceUntilActivation(veteranStack, 0));
	const auto previousActivations = queueActivations(veteranStack);
	ASSERT_GT(previousActivations, 0u);

	applyDamage(veteranStack, 100, battle::DamageProvenance::PHYSICAL_CREATURE);
	const auto woundedHealth = veteranStack->getAvailableHealth();
	ASSERT_EQ(veteranStack->veteranPhysicalDamageSinceActivation, 100);
	server.battleLogLines.clear();

	ASSERT_TRUE(advanceUntilActivation(veteranStack, previousActivations));
	EXPECT_EQ(veteranStack->getAvailableHealth(), woundedHealth + 15);
	EXPECT_EQ(veteranStack->veteranPhysicalDamageSinceActivation, 0);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Veteran restores") != std::string::npos;
	}));
}

TEST_F(NewHorizonsArmorerVeteranTest, UnitUpdateCarriesHistoryAndOlderWritersCannotDiscardIt)
{
	startGame();
	startBattle();
	auto * stack = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(92), 10);
	ASSERT_NE(stack, nullptr);
	applyDamage(stack, 37, battle::DamageProvenance::PHYSICAL_CREATURE);
	ASSERT_EQ(stack->veteranPhysicalDamageSinceActivation, 37);
	ASSERT_TRUE(battle()->hasVeteranDamageHistory());

	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	UnitChanges update(stack->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = stack->acquireState()->save();
	packet.changedStacks.push_back(std::move(update));

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	current.oser & packet;
	current.iser.cb = gameState().get();
	BattleUnitsChanged decoded;
	current.iser & decoded;
	ASSERT_EQ(decoded.changedStacks.size(), 1u);
	EXPECT_EQ(decoded.changedStacks.front().data["state"][damageHistoryKey].Integer(), 37);

	CMemorySerializer oldWriter;
	oldWriter.oser.version = ESerializationVersion::NEW_HORIZONS_LEARNING_MENTOR;
	EXPECT_THROW(oldWriter.oser & packet.changedStacks.front(), std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty());

	CMemorySerializer battleWriter;
	battleWriter.oser.version = ESerializationVersion::CURRENT;
	EXPECT_THROW(battleWriter.oser & *battle(), std::runtime_error)
		<< "Binary battle snapshots must fail closed while UnitChanges is the only state carrier";
	EXPECT_TRUE(battleWriter.extractBuffer().empty());
}
