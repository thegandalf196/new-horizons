/*
 * NewHorizonsBattlefieldMasteryTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/BattleChanges.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>

namespace
{
constexpr auto battlecraftSkill = "new-horizons:battlecraft";
constexpr auto entrenchPerk = "new-horizons:battlecraft.entrench";
constexpr auto reservePerk = "new-horizons:battlecraft.reserve";
constexpr auto passingLinesPerk = "new-horizons:battlecraft.passingLines";
constexpr auto masteryPerk = "new-horizons:battlecraft.battlefieldMastery";

class NewHorizonsBattlefieldMasteryTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void acceptPerk(CGHeroInstance * hero, std::string_view perkId)
	{
		const std::string requestedSkill(battlecraftSkill);
		const std::string requestedPerk(perkId);
		const auto rankLookup = [hero](const std::string & id)
		{
			return hero->getPerkSkillRank(id);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
			{
				if(offers[choice].selection.skillId != requestedSkill
					|| offers[choice].selection.perkId != requestedPerk)
					continue;
				gameHandler->levelUpHero(hero, offers, choice, seed, false);
				ASSERT_TRUE(hero->hasActivePerk(requestedSkill, requestedPerk));
				return;
			}
		}
		FAIL() << "The active perk was not legally offered: " << requestedPerk;
	}

	void selectExpertMastery(CGHeroInstance * hero, bool entrench)
	{
		const auto decoded = SecondarySkill::decode(battlecraftSkill);
		ASSERT_GE(decoded, 0);
		const auto skill = SecondarySkill(decoded);
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, entrench ? entrenchPerk : reservePerk);
		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, passingLinesPerk);
		hero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, masteryPerk);
		ASSERT_EQ(newHorizonsBattlecraft::rank(hero), 3);
	}

	void removeStartingStacks(CStack * retain = nullptr)
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(!retain || unit->unitId() != retain->unitId())
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	bool wait(CStack * stack)
	{
		battle()->activeStack = stack->unitId();
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(stack),
			BattleAction::makeWait(stack));
	}

	bool defend(CStack * stack)
	{
		battle()->activeStack = stack->unitId();
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(stack),
			BattleAction::makeDefend(stack));
	}

	bool act(const battle::Unit * stack, const BattleAction & action)
	{
		return gameHandler->battles->makePlayerBattleAction(
			BattleID(0), battle()->battleGetOwner(stack), action);
	}

	void advanceUntilActive(const CStack * expected)
	{
		for(int attempt = 0; attempt < 24; ++attempt)
		{
			if(battle()->battleActiveUnit() == expected)
				return;
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			ASSERT_TRUE(act(active, BattleAction::makeDefend(active)));
		}
		FAIL() << "The expected stack did not receive its next ordinary activation";
	}

	void nextRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}
};
}

TEST_F(NewHorizonsBattlefieldMasteryTest, FirstEligibleWaitIsOneShotPerSideAndRenewsNextRound)
{
	startGame();
	selectExpertMastery(attackerSideHero, false);
	selectExpertMastery(defenderSideHero, false);
	startBattle();
	removeStartingStacks();
	auto * first = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * second = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex - 4), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	ASSERT_NE(enemy, nullptr);
	forceMaximumDamage(first);
	blockRetaliation(first);
	blockRetaliation(enemy);
	beginCombat();

	const BattleAttackInfo firstAttack(first, enemy, 0, false);
	const auto ordinaryDamage = battle()->calculateDmgRange(firstAttack).damage.max;
	ASSERT_GT(ordinaryDamage, 0);
	EXPECT_EQ(newHorizonsBattlecraft::waitDamagePercent(attackerSideHero, first), 15);
	EXPECT_EQ(battle()->getBattlecraftMasteryAwardRound(BattleSide::ATTACKER), -1);

	ASSERT_TRUE(wait(first));
	ASSERT_TRUE(first->waitedThisTurn);
	ASSERT_TRUE(first->battlecraftWaitMasteryDoubled);
	EXPECT_EQ(battle()->getBattlecraftMasteryAwardRound(BattleSide::ATTACKER), 1);
	EXPECT_EQ(newHorizonsBattlecraft::waitDamagePercent(attackerSideHero, first), 30);
	EXPECT_EQ(battle()->calculateDmgRange(firstAttack).damage.max, ordinaryDamage * 130 / 100);

	UnitChanges update(first->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = first->save();
	BattleUnitsChanged unitUpdate;
	unitUpdate.battleID = BattleID(0);
	unitUpdate.changedStacks.push_back(update);
	CMemorySerializer unitWriter;
	unitWriter.oser.version = ESerializationVersion::CURRENT;
	unitWriter.oser & unitUpdate;
	CMemorySerializer unitReader(unitWriter.extractBuffer());
	unitReader.iser.version = ESerializationVersion::CURRENT;
	BattleUnitsChanged restoredUnitUpdate;
	unitReader.iser & restoredUnitUpdate;
	ASSERT_EQ(restoredUnitUpdate.changedStacks.size(), 1u);
	EXPECT_TRUE(restoredUnitUpdate.changedStacks.front().data["state"]["battlecraftWaitMasteryDoubled"].Bool());

	CMemorySerializer activeMarkerSnapshot;
	activeMarkerSnapshot.oser.version = ESerializationVersion::CURRENT;
	EXPECT_THROW(activeMarkerSnapshot.oser & *battle(), std::runtime_error)
		<< "A binary battle descriptor cannot resume CUnitState Wait/Defend lifetime";

	SetBattlecraftMasteryAward award;
	award.battleID = BattleID(0);
	award.side = BattleSide::ATTACKER;
	award.unitId = first->unitId();
	award.round = 1;
	award.action = BattlecraftMasteryAction::WAIT;
	CMemorySerializer awardWriter;
	awardWriter.oser.version = ESerializationVersion::CURRENT;
	awardWriter.oser & award;
	CMemorySerializer awardReader(awardWriter.extractBuffer());
	awardReader.iser.version = ESerializationVersion::CURRENT;
	SetBattlecraftMasteryAward restoredAward;
	awardReader.iser & restoredAward;
	EXPECT_EQ(restoredAward.battleID, award.battleID);
	EXPECT_EQ(restoredAward.side, award.side);
	EXPECT_EQ(restoredAward.unitId, award.unitId);
	EXPECT_EQ(restoredAward.round, award.round);
	EXPECT_EQ(restoredAward.action, award.action);

	CMemorySerializer oldAwardWriter;
	oldAwardWriter.oser.version = ESerializationVersion::NEW_HORIZONS_SPELL_RESPONSE;
	EXPECT_THROW(oldAwardWriter.oser & award, std::runtime_error);
	EXPECT_TRUE(oldAwardWriter.extractBuffer().empty());

	ASSERT_TRUE(attack(first, enemy->getPosition()));
	EXPECT_TRUE(first->battlecraftWaitBonusUsed);
	EXPECT_FALSE(first->battlecraftWaitMasteryDoubled);
	EXPECT_EQ(newHorizonsBattlecraft::waitDamagePercent(attackerSideHero, first), 15)
		<< "The doubled Wait rank term ends with the existing one-shot physical bonus";
	EXPECT_EQ(battle()->getBattlecraftMasteryAwardRound(BattleSide::ATTACKER), 1)
		<< "Spending the unit effect does not reopen the side's award";
	const auto spentWaitSnapshot = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(spentWaitSnapshot, nullptr);
	EXPECT_EQ(spentWaitSnapshot->getBattlecraftMasteryAwardRound(BattleSide::ATTACKER), 1);
	// The binary descriptor retains the CStack identity but omits its CUnitState;
	// inspect it even if the default omitted alive state is false.
	const auto * snapshotStack = spentWaitSnapshot->getStack(static_cast<int>(first->unitId()), false);
	ASSERT_NE(snapshotStack, nullptr);
	EXPECT_EQ(snapshotStack->unitId(), first->unitId());
	EXPECT_FALSE(snapshotStack->battlecraftWaitMasteryDoubled);
	CMemorySerializer oldBattleWriter;
	oldBattleWriter.oser.version = ESerializationVersion::NEW_HORIZONS_SPELL_RESPONSE;
	EXPECT_THROW(oldBattleWriter.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(oldBattleWriter.extractBuffer().empty());

	ASSERT_TRUE(wait(second));
	EXPECT_FALSE(second->battlecraftWaitMasteryDoubled);
	EXPECT_EQ(newHorizonsBattlecraft::waitDamagePercent(attackerSideHero, second), 15);
	EXPECT_EQ(battle()->getBattlecraftMasteryAwardRound(BattleSide::ATTACKER), 1);

	ASSERT_TRUE(wait(enemy));
	EXPECT_TRUE(enemy->battlecraftWaitMasteryDoubled)
		<< "The opposing side has an independent first-action award";
	EXPECT_EQ(battle()->getBattlecraftMasteryAwardRound(BattleSide::DEFENDER), 1);

	nextRound();
	ASSERT_EQ(battle()->getRound(), 2);
	EXPECT_EQ(battle()->getBattlecraftMasteryAwardRound(BattleSide::ATTACKER), 1)
		<< "The historical stamp remains, but cannot block a later round";
	EXPECT_FALSE(second->battlecraftWaitMasteryDoubled);
	ASSERT_TRUE(wait(second));
	EXPECT_TRUE(second->battlecraftWaitMasteryDoubled);
	EXPECT_EQ(battle()->getBattlecraftMasteryAwardRound(BattleSide::ATTACKER), 2);
	EXPECT_EQ(newHorizonsBattlecraft::waitDamagePercent(attackerSideHero, second), 30);
}

TEST_F(NewHorizonsBattlefieldMasteryTest, DefendDoublesOnlyRankReductionAndExpiresAtNextActivation)
{
	startGame();
	selectExpertMastery(attackerSideHero, true);
	startBattle();
	removeStartingStacks();
	auto * defended = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * probe = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(5, 4), 100);
	auto * striker = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	auto * reserve = addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(14, 5), 100);
	ASSERT_NE(defended, nullptr);
	ASSERT_NE(probe, nullptr);
	ASSERT_NE(striker, nullptr);
	ASSERT_NE(reserve, nullptr);
	forceMaximumDamage(striker);
	blockRetaliation(striker);
	beginCombat();

	BattleAttackInfo attackInfo(striker, defended, 0, false);
	BattleAttackInfo nonPhysical = attackInfo;
	nonPhysical.physicalDamage = false;
	ASSERT_TRUE(defend(defended));
	ASSERT_TRUE(defended->defended());
	ASSERT_TRUE(defended->battlecraftDefendMasteryDoubled);
	EXPECT_EQ(battle()->getBattlecraftMasteryAwardRound(BattleSide::ATTACKER), 1);
	EXPECT_EQ(newHorizonsBattlecraft::defendReductionPercent(attackerSideHero, defended), 35)
		<< "Expert Battlecraft's 15% rank term doubles to 30%; Entrench adds 5 separately";
	EXPECT_EQ(battle()->calculateDmgRange(attackInfo).damage.max,
		battle()->calculateDmgRange(nonPhysical).damage.max * 65 / 100);

	endRound();
	ASSERT_EQ(battle()->getRound(), 2);
	EXPECT_TRUE(defended->defended());
	EXPECT_TRUE(defended->battlecraftDefendMasteryDoubled)
		<< "The captured Defend rank contribution survives rollover until the normal activation";
	EXPECT_EQ(newHorizonsBattlecraft::defendReductionPercent(attackerSideHero, defended), 35);

	advanceUntilActive(defended);
	EXPECT_FALSE(defended->battlecraftDefendMasteryDoubled);
	EXPECT_FALSE(defended->defended());
	EXPECT_EQ(newHorizonsBattlecraft::defendReductionPercent(attackerSideHero, defended), 20)
		<< "At the next queue activation, only the undoubled rank bonus and Entrench remain";
}

TEST_F(NewHorizonsBattlefieldMasteryTest, WarMachineWaitDoesNotReceiveOrConsumeAward)
{
	startGame();
	selectExpertMastery(attackerSideHero, false);
	giveArtifact(attackerSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
	startBattle();
	const auto units = battle()->battleGetAllUnits(false);
	const auto machine = std::find_if(units.begin(), units.end(), [](const auto * unit)
		{ return unit && unit->unitSlot() == SlotID::WAR_MACHINES_SLOT; });
	ASSERT_NE(machine, units.end());
	auto * ballista = battle()->getStack((*machine)->unitId());
	ASSERT_NE(ballista, nullptr);
	EXPECT_TRUE(ballista->isBallista());
	removeStartingStacks(ballista);
	auto * ordinary = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	ASSERT_NE(ordinary, nullptr);
	ASSERT_NE(enemy, nullptr);
	beginCombat();

	ASSERT_TRUE(wait(ballista));
	EXPECT_FALSE(ballista->battlecraftWaitMasteryDoubled);
	EXPECT_EQ(battle()->getBattlecraftMasteryAwardRound(BattleSide::ATTACKER), -1)
		<< "An ineligible War Machine neither receives nor consumes the side opportunity";

	ASSERT_TRUE(wait(ordinary));
	EXPECT_TRUE(ordinary->battlecraftWaitMasteryDoubled);
	EXPECT_EQ(battle()->getBattlecraftMasteryAwardRound(BattleSide::ATTACKER), 1);
}
