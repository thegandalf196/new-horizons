/*
 * NewHorizonsLearningFieldStudyTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "battles/BattleStartSnapshotFixture.h"
#include "battles/BattleTestFixture.h"
#include "battles/FullGameSnapshotTypes.h"

#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/battle/SideInBattle.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/ESerializationVersion.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"
#include "../../server/queries/BattleQueries.h"
#include "../../server/queries/QueriesProcessor.h"
#include "../mock/TinyH3MBuilder.h"

#include <vstd/ContainerUtils.h>

namespace
{
constexpr auto LEARNING_SKILL = "new-horizons:learning";
constexpr auto LEARNING_MENTOR = "new-horizons:learning.mentor";
constexpr auto LEARNING_QUICK_STUDY = "new-horizons:learning.quickStudy";
constexpr auto FIELD_STUDY = "new-horizons:learning.fieldStudy";

CreatureID creature(const char * id)
{
	return CreatureID(CreatureID::decode(id));
}

struct ComparisonCase
{
	const char * name;
	int32_t attackerCount;
	int32_t defenderCount;
	bool fieldStudySelected;
	bool reduceLearningRank;
};

struct BattleXpContext
{
	BattleSide referenceWinner = BattleSide::ATTACKER;
	const CGHeroInstance * winnerHero = nullptr;
	const CGHeroInstance * loserHero = nullptr;
	bool defendedTown = false;
};

class NewHorizonsLearningFieldStudyTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings);
		if(settings.color == PlayerColor(0))
			settings.connectedPlayerIDs.clear();
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		bool foundFieldStudy = false;
		for(auto & perk : perkRules["skills"][LEARNING_SKILL]["perks"].Vector())
		{
			if(perk["id"].String() != FIELD_STUDY)
				continue;

			foundFieldStudy = true;
			if(perk["effect"]["status"].String() == "planned")
				perk["effect"]["status"].String() = "active";
		}
		if(!foundFieldStudy)
			throw std::runtime_error("Missing Field Study from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	static SecondarySkill learningSkill()
	{
		const auto decoded = SecondarySkill::decode(LEARNING_SKILL);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	static bool selectOfferedPerk(CGameHandler & handler, CGHeroInstance * hero,
		std::string_view perkId, MasteryLevel::Type requiredRank)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};
		for(uint64_t seed = 0; seed < 10000; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t index = 0; index < offer.size(); ++index)
			{
				if(offer[index].selection.perkId != perkId)
					continue;
				if(offer[index].requiredRank != requiredRank)
					return false;
				handler.levelUpHero(hero, offer, index, seed, false);
				return true;
			}
		}
		return false;
	}

	void acquireFieldStudy(CGHeroInstance * hero)
	{
		ASSERT_NE(hero, nullptr);
		const auto learning = learningSkill();
		gameHandler->changeSecSkill(hero, learning, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		gameHandler->levelUpHero(hero, learning, false);
		ASSERT_EQ(hero->getSecSkillLevel(learning), MasteryLevel::BASIC);
		ASSERT_TRUE(selectOfferedPerk(*gameHandler, hero, LEARNING_MENTOR, MasteryLevel::BASIC))
			<< "The legal Basic Learning offer must be accepted before advancing";
		EXPECT_FALSE(hero->hasActivePerk(LEARNING_SKILL, FIELD_STUDY));

		gameHandler->levelUpHero(hero, learning, false);
		ASSERT_EQ(hero->getSecSkillLevel(learning), MasteryLevel::ADVANCED);
		ASSERT_TRUE(selectOfferedPerk(*gameHandler, hero, FIELD_STUDY, MasteryLevel::ADVANCED))
			<< "Field Study must appear only in a legal Advanced Learning offer";
		ASSERT_TRUE(hero->hasActivePerk(LEARNING_SKILL, FIELD_STUDY));
	}

	void advanceLearningWithOtherAdvancedPerk(CGHeroInstance * hero)
	{
		ASSERT_NE(hero, nullptr);
		const auto learning = learningSkill();
		gameHandler->changeSecSkill(hero, learning, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		gameHandler->levelUpHero(hero, learning, false);
		ASSERT_TRUE(selectOfferedPerk(*gameHandler, hero, LEARNING_MENTOR, MasteryLevel::BASIC));
		gameHandler->levelUpHero(hero, learning, false);
		ASSERT_EQ(hero->getSecSkillLevel(learning), MasteryLevel::ADVANCED);
		ASSERT_TRUE(selectOfferedPerk(*gameHandler, hero, LEARNING_QUICK_STUDY, MasteryLevel::ADVANCED));
		EXPECT_FALSE(hero->hasActivePerk(LEARNING_SKILL, FIELD_STUDY));
	}

	void setHeroArmy(CGHeroInstance * hero, CreatureID type, int32_t count)
	{
		ASSERT_NE(hero, nullptr);
		hero->clearSlots();
		ASSERT_TRUE(hero->setCreature(SlotID(0), type, count));
	}

	void startMonsterBattle(int32_t heroCount, int32_t monsterCount)
	{
		const auto pikeman = creature("core:pikeman");
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("FieldStudyMonsterBattle")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroGarrison({{CreatureID(0), 1}})
			.monster({12, 12, 0}, pikeman, static_cast<uint16_t>(monsterCount),
				static_cast<int8_t>(CGCreature::Character::HOSTILE));
		startWithMap(std::move(builder));

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		ASSERT_NE(attackerSideHero, nullptr);
		attackerSideHero->clearSlots();
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), pikeman, heroCount));
	}

	CStack * stackAt(BattleSide side, SlotID slot) const
	{
		const auto stacks = battle()->battleGetStacksIf([side, slot](const CStack * stack)
		{
			return stack->unitSide() == side && stack->unitSlot() == slot;
		});
		if(stacks.size() != 1)
			return nullptr;
		return const_cast<CStack *>(stacks.front());
	}

	std::shared_ptr<CBattleQuery> battleQuery()
	{
		const auto existing = std::dynamic_pointer_cast<CBattleQuery>(
			gameHandler->queries->topQuery(PlayerColor(0)));
		if(existing)
			return existing;

		auto query = std::make_shared<CBattleQuery>(gameHandler.get(), battle());
		gameHandler->queries->addQuery(query);
		return query;
	}

	std::shared_ptr<CBattleQuery> finishBattle(BattleSide winner)
	{
		auto query = battleQuery();
		gameHandler->battles->cheatBattleVictory(battle()->getSidePlayer(winner));
		return query;
	}

	void acceptBattleResult()
	{
		for(const auto player : {PlayerColor(0), PlayerColor(1)})
		{
			auto query = gameHandler->queries->topQuery(player);
			if(query && query->getType() == QueryType::BattleDialog)
			{
				ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, player));
			}
		}
	}

	BattleXpContext captureBattleXpContext(BattleSide winner) const
	{
		const auto loser = winner == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
		return BattleXpContext{
			winner,
			battle()->battleGetFightingHero(winner),
			battle()->battleGetFightingHero(loser),
			battle()->battleGetDefendedTown() != nullptr};
	}

	static TExpType casualtyBaseXp(const BattleResult & result, BattleSide winner,
		const BattleXpContext & context)
	{
		const BattleSide loser = winner == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
		int64_t baseXp = 0;
		for(const auto & [creatureId, count] : result.casualties[loser])
			baseXp += creatureId.toCreature()->valOfBonuses(BonusType::STACK_HEALTH) * count;

		const auto * defeatedHero = winner == context.referenceWinner ? context.loserHero : context.winnerHero;
		if(result.result == EBattleResult::NORMAL && defeatedHero)
			baseXp += 500;
		if(context.defendedTown && winner == BattleSide::ATTACKER)
			baseXp += 500;
		return static_cast<TExpType>(baseXp);
	}

	void expectWinnerXp(const std::shared_ptr<CBattleQuery> & query, BattleSide winner,
		int32_t additionalPercent, const BattleXpContext & context)
	{
		ASSERT_NE(query, nullptr);
		ASSERT_TRUE(query->result);
		const auto * hero = context.winnerHero;
		ASSERT_NE(hero, nullptr);
		const auto & result = *query->result;
		ASSERT_EQ(result.winner, winner);
		const auto baseXp = casualtyBaseXp(result, winner, context);
		if(additionalPercent == 25)
		{
			EXPECT_EQ(hero->valOfBonuses(BonusType::HERO_EXPERIENCE_GAIN_PERCENT), 120);
			const auto expectedWithFieldStudy = static_cast<TExpType>(baseXp * 145.0 / 100.0);
			EXPECT_EQ(result.exp[winner], expectedWithFieldStudy)
				<< "Advanced Learning's 120% ordinary rate plus Field Study's 25 points is 145%";
			EXPECT_GT(result.exp[winner], hero->calculateXp(baseXp));
		}
		else
			EXPECT_EQ(result.exp[winner], hero->calculateXp(baseXp));

		const BattleSide loser = winner == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
		if(const auto * losingHero = context.loserHero)
		{
			const auto losingHeroBaseXp = casualtyBaseXp(result, loser, context);
			EXPECT_EQ(result.exp[loser], losingHero->calculateXp(losingHeroBaseXp))
				<< "Field Study must not modify the non-winning hero's ordinary result XP";
		}
	}

	void damageStack(CStack * target, int64_t damage)
	{
		ASSERT_NE(target, nullptr);
		ASSERT_GT(damage, 0);
		StacksInjured injured;
		injured.battleID = BattleID(0);
		auto & attack = injured.stacks.emplace_back();
		attack.attackerID = target->unitId();
		attack.stackAttacked = target->unitId();
		attack.damageAmount = damage;
		target->prepareAttacked(attack, gameHandler->getRandomGenerator());
		gameHandler->sendAndApply(injured);
	}

};

class NewHorizonsLearningFieldStudyComparisonTest : public NewHorizonsLearningFieldStudyTest,
	public ::testing::WithParamInterface<ComparisonCase>
{};
}

TEST_F(NewHorizonsLearningFieldStudyTest, StrongerEnemyHeroRewardsTheAttackerFromFrozenRawArmyValues)
{
	startGame();
	const auto pikeman = creature("core:pikeman");
	setHeroArmy(attackerSideHero, pikeman, 10);
	setHeroArmy(defenderSideHero, pikeman, 20);
	acquireFieldStudy(attackerSideHero);

	const auto attackerArmyValue = attackerSideHero->getArmyStrength();
	const auto defenderArmyValue = defenderSideHero->getArmyStrength();
	ASSERT_GT(defenderArmyValue, attackerArmyValue);
	startBattle();
	ASSERT_EQ(battle()->getSide(BattleSide::ATTACKER).initialArmyValue, attackerArmyValue);
	ASSERT_EQ(battle()->getSide(BattleSide::DEFENDER).initialArmyValue, defenderArmyValue);
	EXPECT_EQ(battle()->getSide(BattleSide::DEFENDER).heroID, defenderSideHero->id);

	// Let the stronger army suffer casualties after the snapshots were captured.
	// The live field is now weaker than the winner, but the original comparison remains authoritative.
	auto * defenderStack = stackAt(BattleSide::DEFENDER, SlotID(0));
	ASSERT_NE(defenderStack, nullptr);
	const auto healthPerCreature = defenderStack->unitType()->valOfBonuses(BonusType::STACK_HEALTH);
	const int64_t damage = static_cast<int64_t>(15) * healthPerCreature;
	damageStack(defenderStack, damage);
	ASSERT_EQ(defenderStack->getCount(), 5);
	EXPECT_EQ(battle()->getSide(BattleSide::DEFENDER).initialArmyValue, defenderArmyValue);

	const auto xpContext = captureBattleXpContext(BattleSide::ATTACKER);
	const auto expBeforeResult = attackerSideHero->exp;
	const auto query = finishBattle(BattleSide::ATTACKER);
	expectWinnerXp(query, BattleSide::ATTACKER, 25, xpContext);
	const auto publishedXp = query->result->exp[BattleSide::ATTACKER];
	acceptBattleResult();
	EXPECT_EQ(attackerSideHero->exp - expBeforeResult, publishedXp)
		<< "The battle-result dialog must award the same XP amount that Field Study published";
}

TEST_F(NewHorizonsLearningFieldStudyTest, StrongerEnemyHeroRewardsTheDefenderWinner)
{
	startGame();
	const auto pikeman = creature("core:pikeman");
	setHeroArmy(attackerSideHero, pikeman, 20);
	setHeroArmy(defenderSideHero, pikeman, 10);
	acquireFieldStudy(defenderSideHero);

	const auto attackerArmyValue = attackerSideHero->getArmyStrength();
	const auto defenderArmyValue = defenderSideHero->getArmyStrength();
	ASSERT_GT(attackerArmyValue, defenderArmyValue);
	startBattle();
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).initialArmyValue, attackerArmyValue);
	EXPECT_EQ(battle()->getSide(BattleSide::DEFENDER).initialArmyValue, defenderArmyValue);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).heroID, attackerSideHero->id);

	const auto xpContext = captureBattleXpContext(BattleSide::DEFENDER);
	const auto query = finishBattle(BattleSide::DEFENDER);
	expectWinnerXp(query, BattleSide::DEFENDER, 25, xpContext);
}

TEST_F(NewHorizonsLearningFieldStudyTest, StrongerWanderingMonsterRewardsTheHero)
{
	startMonsterBattle(10, 20);
	acquireFieldStudy(attackerSideHero);
	const auto heroArmyValue = attackerSideHero->getArmyStrength();
	const auto * monster = findFirst<CGCreature>();
	ASSERT_NE(monster, nullptr);
	ASSERT_EQ(monster->ID, Obj::MONSTER);
	const auto monsterArmyValue = monster->getArmyStrength();
	ASSERT_GT(monsterArmyValue, heroArmyValue);

	const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, monster);
	gameHandler->startBattle(attackerSideHero, monster, monster->visitablePos(),
		attackerSideHero, nullptr, layout, nullptr);
	ASSERT_EQ(battle()->getSide(BattleSide::ATTACKER).initialArmyValue, heroArmyValue);
	ASSERT_EQ(battle()->getSide(BattleSide::DEFENDER).initialArmyValue, monsterArmyValue);
	EXPECT_TRUE(battle()->getSide(BattleSide::DEFENDER).initialArmyIsWandering);
	EXPECT_FALSE(battle()->getSide(BattleSide::DEFENDER).heroID.hasValue());

	const auto xpContext = captureBattleXpContext(BattleSide::ATTACKER);
	const auto query = finishBattle(BattleSide::ATTACKER);
	expectWinnerXp(query, BattleSide::ATTACKER, 25, xpContext);
}

TEST_P(NewHorizonsLearningFieldStudyComparisonTest, OnlySelectedAdvancedStudyAndStrictlyGreaterStartValueQualify)
{
	const auto & scenario = GetParam();
	SCOPED_TRACE(scenario.name);
	startGame();
	const auto pikeman = creature("core:pikeman");
	setHeroArmy(attackerSideHero, pikeman, scenario.attackerCount);
	setHeroArmy(defenderSideHero, pikeman, scenario.defenderCount);
	if(scenario.fieldStudySelected)
		acquireFieldStudy(attackerSideHero);
	else
		advanceLearningWithOtherAdvancedPerk(attackerSideHero);
	if(scenario.reduceLearningRank)
	{
		gameHandler->changeSecSkill(attackerSideHero, learningSkill(), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		ASSERT_FALSE(attackerSideHero->hasActivePerk(LEARNING_SKILL, FIELD_STUDY));
	}

	const auto attackerArmyValue = attackerSideHero->getArmyStrength();
	const auto defenderArmyValue = defenderSideHero->getArmyStrength();
	startBattle();
	ASSERT_EQ(battle()->getSide(BattleSide::ATTACKER).initialArmyValue, attackerArmyValue);
	ASSERT_EQ(battle()->getSide(BattleSide::DEFENDER).initialArmyValue, defenderArmyValue);
	EXPECT_FALSE(battle()->getSide(BattleSide::DEFENDER).initialArmyIsWandering);

	const auto xpContext = captureBattleXpContext(BattleSide::ATTACKER);
	const auto query = finishBattle(BattleSide::ATTACKER);
	expectWinnerXp(query, BattleSide::ATTACKER, 0, xpContext);
}

INSTANTIATE_TEST_SUITE_P(EligibilityConditions, NewHorizonsLearningFieldStudyComparisonTest,
	::testing::Values(
		ComparisonCase{"EqualStartArmyValues", 10, 10, true, false},
		ComparisonCase{"WeakerEnemyArmy", 10, 5, true, false},
		ComparisonCase{"NoSelectedFieldStudy", 10, 20, false, false},
		ComparisonCase{"FieldStudyBelowRequiredRank", 10, 20, true, true}),
	[](const ::testing::TestParamInfo<ComparisonCase> & info)
	{
		return std::string(info.param.name);
	});

TEST_F(NewHorizonsLearningFieldStudyTest, TownGarrisonWithoutFightingHeroDoesNotQualify)
{
	startGame(true);
	const auto pikeman = creature("core:pikeman");
	setHeroArmy(attackerSideHero, pikeman, 10);
	acquireFieldStudy(attackerSideHero);
	const auto attackerArmyValue = attackerSideHero->getArmyStrength();

	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(town, nullptr);
	town->clearSlots();
	ASSERT_TRUE(town->setCreature(SlotID(0), pikeman, 20));
	const auto townArmyValue = town->getArmyStrength();
	ASSERT_GT(townArmyValue, attackerArmyValue);
	const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, town);
	gameHandler->startBattle(attackerSideHero, town, town->visitablePos(),
		attackerSideHero, nullptr, layout, town);
	ASSERT_FALSE(battle()->getSide(BattleSide::DEFENDER).heroID.hasValue());
	EXPECT_FALSE(battle()->getSide(BattleSide::DEFENDER).initialArmyIsWandering);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).initialArmyValue, attackerArmyValue);
	EXPECT_EQ(battle()->getSide(BattleSide::DEFENDER).initialArmyValue, townArmyValue);

	const auto xpContext = captureBattleXpContext(BattleSide::ATTACKER);
	const auto query = finishBattle(BattleSide::ATTACKER);
	expectWinnerXp(query, BattleSide::ATTACKER, 0, xpContext);
}

TEST_F(NewHorizonsLearningFieldStudyTest, InitialArmySnapshotRoundTripsBoundariesDefaultsAndRejectsOldWriters)
{
	startGame();
	const auto pikeman = creature("core:pikeman");
	setHeroArmy(attackerSideHero, pikeman, 10);
	setHeroArmy(defenderSideHero, pikeman, 20);
	startBattle();

	const auto actualAttackerValue = attackerSideHero->getArmyStrength();
	const auto actualDefenderValue = defenderSideHero->getArmyStrength();
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).initialArmyValue, actualAttackerValue);
	EXPECT_EQ(battle()->getSide(BattleSide::DEFENDER).initialArmyValue, actualDefenderValue);

	BattleStart currentSource;
	currentSource.battleID = BattleID(0);
	currentSource.info = battleStartFixture::snapshot(*battle(), gameState().get());
	ASSERT_NE(currentSource.info, nullptr);
	currentSource.info->getSide(BattleSide::ATTACKER).initialArmyValue = std::numeric_limits<uint64_t>::max();
	currentSource.info->getSide(BattleSide::ATTACKER).initialArmyIsWandering = true;
	currentSource.info->getSide(BattleSide::DEFENDER).initialArmyValue = uint64_t{0};
	currentSource.info->getSide(BattleSide::DEFENDER).initialArmyIsWandering = false;

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	current.oser & currentSource;
	current.iser.cb = gameState().get();
	BattleStart restored;
	current.iser & restored;
	ASSERT_NE(restored.info, nullptr);
	EXPECT_EQ(restored.info->getSide(BattleSide::ATTACKER).initialArmyValue,
		std::optional<uint64_t>(std::numeric_limits<uint64_t>::max()));
	EXPECT_TRUE(restored.info->getSide(BattleSide::ATTACKER).initialArmyIsWandering);
	EXPECT_EQ(restored.info->getSide(BattleSide::DEFENDER).initialArmyValue, std::optional<uint64_t>(0));
	EXPECT_FALSE(restored.info->getSide(BattleSide::DEFENDER).initialArmyIsWandering);

	CMemorySerializer oldWriter;
	oldWriter.oser.version = ESerializationVersion::NEW_HORIZONS_INVESTOR_INCOME;
	EXPECT_THROW(oldWriter.oser & currentSource, std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty())
		<< "An old BattleStart writer must reject a captured Army Value before writing bytes";

	BattleStart legacySource;
	legacySource.battleID = BattleID(0);
	legacySource.info = battleStartFixture::snapshot(*battle(), gameState().get());
	ASSERT_NE(legacySource.info, nullptr);
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		legacySource.info->getSide(side).initialArmyValue.reset();
		legacySource.info->getSide(side).initialArmyIsWandering = false;
	}
	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_INVESTOR_INCOME;
	legacy.iser.version = ESerializationVersion::NEW_HORIZONS_INVESTOR_INCOME;
	legacy.oser & legacySource;
	legacy.iser.cb = gameState().get();
	BattleStart legacyRestored;
	legacy.iser & legacyRestored;
	ASSERT_NE(legacyRestored.info, nullptr);
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		EXPECT_FALSE(legacyRestored.info->getSide(side).initialArmyValue.has_value());
		EXPECT_FALSE(legacyRestored.info->getSide(side).initialArmyIsWandering);
	}
}
