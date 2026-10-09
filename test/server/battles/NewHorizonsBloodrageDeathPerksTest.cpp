/*
 * NewHorizonsBloodrageDeathPerksTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../server/CGameHandler.h"
#include "../../../lib/battle/NewHorizonsBloodrage.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"

namespace
{
constexpr std::string_view SKILL = "new-horizons:bloodrage";
constexpr std::string_view FIRST = "new-horizons:bloodrage.firstBlood";
constexpr std::string_view SLAYER = "new-horizons:bloodrage.slayer";
class DeathPerksEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit DeathPerksEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class NewHorizonsBloodrageDeathPerksTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires production-active New Horizons content";
	}
	void select(std::string_view id, CGHeroInstance * hero = nullptr)
	{
		if(!hero)
			hero = attackerSideHero;
		const auto lookup = [hero](const std::string & key) { return hero->getPerkSkillRank(key); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(lookup, seed);
			for(size_t i = 0; i < offers.size(); ++i)
				if(offers[i].selection.skillId == SKILL && offers[i].selection.perkId == id)
				{
					gameHandler->levelUpHero(hero, offers, i, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(std::string(SKILL), std::string(id)));
					return;
				}
		}
		FAIL() << "No legal active Bloodrage perk offer " << id;
	}
	void prepare(bool first = true, bool slayer = true, bool bothHolders = false)
	{
		startGame();
		const SecondarySkill skill(SecondarySkill::decode(std::string(SKILL)));
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(select(first ? FIRST : "new-horizons:bloodrage.warDrums"));
		if(slayer)
		{
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			ASSERT_NO_FATAL_FAILURE(select(SLAYER));
		}
		if(bothHolders)
		{
			defenderSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			ASSERT_NO_FATAL_FAILURE(select(FIRST, defenderSideHero));
			defenderSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			ASSERT_NO_FATAL_FAILURE(select(SLAYER, defenderSideHero));
		}
		startBattle();
		// Remove the optional control's War Drums opening increment only, not any
		// destruction receipt; each scenario starts before its first real death.
		battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = 0;
		ASSERT_FALSE(battle()->bloodrageFirstBloodUsed);
	}
	CStack * victim(const std::string & name, BattleSide side = BattleSide::DEFENDER, int offset = 0)
	{
		return addStack(side, creatureByName(name), BattleHex(rightHex + offset), 1);
	}
	void kill(CStack * unit)
	{
		StacksInjured packet;
		packet.battleID = BattleID(0);
		auto & hit = packet.stacks.emplace_back();
		hit.stackAttacked = unit->unitId();
		hit.damageAmount = unit->getAvailableHealth();
		unit->prepareAttacked(hit, gameHandler->getRandomGenerator());
		ASSERT_TRUE(hit.killed());
		ASSERT_FALSE(hit.willRebirth());
		gameHandler->sendAndApply(packet);
	}
};
}

TEST(NewHorizonsBloodrageDeathPerksRulesTest, IndependentExtrasComposeWithoutMultiplication)
{
	EXPECT_EQ(newHorizonsBloodrage::deathIncrementCount(true, true, true, true), 3);
	EXPECT_EQ(newHorizonsBloodrage::deathIncrementCount(true, false, true, true), 2);
	EXPECT_EQ(newHorizonsBloodrage::deathIncrementCount(false, true, true, true), 2);
	EXPECT_EQ(newHorizonsBloodrage::deathIncrementCount(false, false, true, true), 1);
	EXPECT_EQ(newHorizonsBloodrage::deathIncrementCount(true, true, false, false), 1);
}

TEST_F(NewHorizonsBloodrageDeathPerksTest, FirstCoreDeathDoublesOnlyOnceIncludingFriendlyDeath)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, false));
	auto * first = victim("core:pikeman", BattleSide::ATTACKER);
	ASSERT_NO_FATAL_FAILURE(kill(first));
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 10);
	EXPECT_TRUE(battle()->bloodrageFirstBloodUsed);
	battle()->recordBloodrageStackDeath(first->unitId());
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 10);
	ASSERT_NO_FATAL_FAILURE(kill(victim("core:pikeman", BattleSide::DEFENDER, 1)));
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 15);
}

TEST_F(NewHorizonsBloodrageDeathPerksTest, FirstEliteGetsThreeAndLaterChampionTwoIncrements)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(kill(victim("core:ogre")));
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 24);
	ASSERT_NO_FATAL_FAILURE(kill(victim("core:angel", BattleSide::DEFENDER, 1)));
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 40);
}

TEST_F(NewHorizonsBloodrageDeathPerksTest, BothHoldersReceiveFirstEventBeforeSharedReceiptIsConsumed)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, true, true));
	ASSERT_TRUE(newHorizonsBloodrage::hasFirstBlood(attackerSideHero));
	ASSERT_TRUE(newHorizonsBloodrage::hasFirstBlood(defenderSideHero));
	ASSERT_TRUE(newHorizonsBloodrage::hasSlayer(attackerSideHero));
	ASSERT_TRUE(newHorizonsBloodrage::hasSlayer(defenderSideHero));
	ASSERT_NO_FATAL_FAILURE(kill(victim("core:ogre")));
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 24);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::DEFENDER), 24);
	ASSERT_TRUE(battle()->bloodrageFirstBloodUsed);
	ASSERT_NO_FATAL_FAILURE(kill(victim("core:pikeman", BattleSide::ATTACKER, 1)));
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 32);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::DEFENDER), 32);
	EXPECT_TRUE(battle()->bloodrageFirstBloodUsed);
}

TEST_F(NewHorizonsBloodrageDeathPerksTest, SlayerCoreNegativeAndElitePositiveWithoutFirstBlood)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	ASSERT_NO_FATAL_FAILURE(kill(victim("core:pikeman")));
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 8);
	EXPECT_FALSE(battle()->bloodrageFirstBloodUsed);
	ASSERT_NO_FATAL_FAILURE(kill(victim("core:ogre", BattleSide::DEFENDER, 1)));
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 24);
}

TEST_F(NewHorizonsBloodrageDeathPerksTest, CappedFirstDeathSpendsReceiptAndResurrectionDoesNotResetIt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * unit = victim("core:ogre");
	const auto livingState = unit->acquireState()->save();
	battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = 40;
	ASSERT_NO_FATAL_FAILURE(kill(unit));
	ASSERT_TRUE(battle()->bloodrageFirstBloodUsed);
	BattleUnitsChanged revive;
	revive.battleID = BattleID(0);
	revive.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::UPDATE);
	revive.changedStacks.back().data = livingState;
	gameHandler->sendAndApply(revive);
	ASSERT_TRUE(unit->alive());
	ASSERT_TRUE(battle()->bloodrageFirstBloodUsed);
	battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = 0;
	ASSERT_NO_FATAL_FAILURE(kill(unit));
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 16);
}

TEST_F(NewHorizonsBloodrageDeathPerksTest, SummonedAndCloneDeathsDoNotSpendFirstBlood)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * summoned = victim("core:ogre");
	summoned->summoned = true;
	ASSERT_NO_FATAL_FAILURE(kill(summoned));
	EXPECT_FALSE(battle()->bloodrageFirstBloodUsed);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 0);
	auto * clone = victim("core:ogre", BattleSide::DEFENDER, 1);
	BattleUnitsChanged cloned;
	cloned.battleID = BattleID(0);
	cloned.changedStacks.emplace_back(clone->unitId(), UnitChanges::EOperation::UPDATE);
	cloned.changedStacks.back().data = clone->save();
	cloned.changedStacks.back().data["state"]["cloned"].Bool() = true;
	gameHandler->sendAndApply(cloned);
	ASSERT_NO_FATAL_FAILURE(kill(clone));
	EXPECT_FALSE(battle()->bloodrageFirstBloodUsed);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 0);
}

TEST_F(NewHorizonsBloodrageDeathPerksTest, DetachedNestedDeathReceiptMatchesLiveWithoutParentMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * unit = victim("core:ogre");
	DeathPerksEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	parent->getForUpdate(unit->unitId());
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto projected = child->getForUpdate(unit->unitId());
	int64_t lethal = projected->getAvailableHealth();
	projected->damage(lethal);
	child->recordBloodrageTransition(projected, true);
	EXPECT_EQ(child->getBloodrageDamagePercent(BattleSide::ATTACKER), 24);
	EXPECT_EQ(parent->getBloodrageDamagePercent(BattleSide::ATTACKER), 0);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 0);
	ASSERT_NO_FATAL_FAILURE(kill(unit));
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 24);
}

TEST_F(NewHorizonsBloodrageDeathPerksTest, CurrentReceiptPersistsAndOldWriterRejectsBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(kill(victim("core:ogre")));
	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	EXPECT_TRUE(restored->bloodrageFirstBloodUsed);
	EXPECT_EQ(restored->getBloodrageDamagePercent(BattleSide::ATTACKER), 24);
	CMemorySerializer oldWriter;
	oldWriter.oser.version = ESerializationVersion::NEW_HORIZONS_FROZEN;
	EXPECT_THROW(battle()->serialize(oldWriter.oser), std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty());
}

TEST_F(NewHorizonsBloodrageDeathPerksTest, OlderBattleReadDefaultsReceiptToFalse)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false));
	CMemorySerializer old;
	old.oser.version = old.iser.version = ESerializationVersion::NEW_HORIZONS_FROZEN;
	ASSERT_NO_THROW(old.oser & *battle());
	old.iser.cb = gameState().get();
	BattleInfo restored(gameState().get());
	restored.bloodrageFirstBloodUsed = true;
	ASSERT_NO_THROW(old.iser & restored);
	EXPECT_FALSE(restored.bloodrageFirstBloodUsed);
	EXPECT_EQ(restored.getBloodrageDamagePercent(BattleSide::ATTACKER), 0);
}

TEST(NewHorizonsBloodrageDeathPerksRulesTest, OuterBattleStartRejectsReceiptBeforePrefixAndPlainLegacyRemainsLegal)
{
	BattleStart packet;
	packet.battleID = BattleID(0);
	packet.info = std::make_unique<BattleInfo>(nullptr);
	packet.info->bloodrageFirstBloodUsed = true;
	CMemorySerializer writer;
	writer.oser.version = ESerializationVersion::NEW_HORIZONS_FROZEN;
	EXPECT_THROW(packet.serialize(writer.oser), std::runtime_error);
	EXPECT_TRUE(writer.extractBuffer().empty());
	packet.info->bloodrageFirstBloodUsed = false;
	EXPECT_NO_THROW(packet.info->validateBloodrageDeathPerksSerialization(writer.oser));
}
