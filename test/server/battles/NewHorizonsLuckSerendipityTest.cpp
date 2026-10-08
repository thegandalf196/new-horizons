/*
 * NewHorizonsLuckSerendipityTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/serializer/CMemorySerializer.h"

namespace
{
constexpr auto LUCK_SKILL = "new-horizons:luck";
constexpr auto SERENDIPITY = "new-horizons:luck.serendipity";

class SerendipityEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit SerendipityEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class NewHorizonsLuckSerendipityTest : public HeroCommandFixture
{
protected:
	CStack * source = nullptr;
	CStack * ally = nullptr;
	CStack * target = nullptr;
	int goodChance = 0;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		auto & perks = rules["skills"][LUCK_SKILL]["perks"].Vector();
		const auto perk = std::ranges::find_if(perks, [](const auto & item) { return item["id"].String() == SERENDIPITY; });
		if(perk == perks.end())
			throw std::runtime_error("Missing generic Luck Serendipity registry entry");
		RecordProperty("serendipity_registry_status", (*perk)["effect"]["status"].String());
		if((*perk)["effect"]["status"].String() == "planned")
		{
			(*perk)["effect"]["status"].String() = "active";
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, rules);
		}
		JsonNode good;
		JsonNode bad;
		for(int i = 0; i < 10; ++i)
		{
			good.Vector().emplace_back(goodChance);
			bad.Vector().emplace_back(0);
		}
		loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, good);
		loaded->overrideGameSetting(EGameSettings::COMBAT_BAD_LUCK_CHANCE, bad);
		loaded->overrideGameSetting(EGameSettings::COMBAT_LUCK_DICE_SIZE, JsonNode(100));
	}

	void acceptPerk(CGHeroInstance * hero, const char * id)
	{
		const auto rank = [hero](const std::string & skill) { return hero->getPerkSkillRank(skill); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rank, seed);
			const auto choice = std::ranges::find_if(offer, [id](const auto & item)
			{
				return item.selection.skillId == LUCK_SKILL && item.selection.perkId == id;
			});
			if(choice == offer.end())
				continue;
			gameHandler->levelUpHero(hero, offer, std::distance(offer.begin(), choice), seed, false);
			ASSERT_TRUE(hero->hasActivePerk(LUCK_SKILL, id));
			return;
		}
		FAIL() << "No legal perk offer for " << id;
	}
	void acquire(CGHeroInstance * hero)
	{
		const auto skill = SecondarySkill(SecondarySkill::decode(LUCK_SKILL));
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(acceptPerk(hero, "new-horizons:luck.secondChance"));
		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(acceptPerk(hero, SERENDIPITY));
	}
	void prepare(bool selected = true, bool opposingSelected = false)
	{
		startGame();
		if(selected)
		{
			ASSERT_NO_FATAL_FAILURE(acquire(attackerSideHero));
		}
		if(opposingSelected)
		{
			ASSERT_NO_FATAL_FAILURE(acquire(defenderSideHero));
		}
		startBattle();
		BattleUnitsChanged removed;
		removed.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			removed.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(removed);
		source = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 100);
		ally = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(76), 100);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 10000);
		forceMaximumDamage(source);
		blockRetaliation(source);
		beginCombat();
		const int ordinaryLuck = battle()->battleGetAttackLuck(source, target, false);
		source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::LUCK, BonusSource::OTHER, -ordinaryLuck, BonusSourceID()));
		activate(source);
		ASSERT_EQ(battle()->battleGetRound(), 1);
		EXPECT_EQ(battle()->getLuckSerendipityState(BattleSide::ATTACKER).enabled, selected);
	}
	void activate(CStack * stack)
	{
		if(!stack || battle()->getStack(stack->unitId()) != stack || !stack->alive())
			throw std::runtime_error("Serendipity fixture requires a living activation source");
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = stack->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}
	bool strike(CStack * attacker)
	{
		activate(attacker);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(attacker),
			BattleAction::makeMeleeAttack(attacker, target->getPosition(), attacker->getPosition()));
	}
};

TEST_F(NewHorizonsLuckSerendipityTest, RoundTwoOnlyFirstAcceptedFriendlyAttackUsesSharedOpportunity)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, true));
	EXPECT_EQ(battle()->battleGetAttackLuck(source, target, false), 0);
	ASSERT_TRUE(strike(source));
	EXPECT_FALSE(battle()->getLuckSerendipityState(BattleSide::ATTACKER).currentRoundPositiveLuck);
	advanceRound();
	ASSERT_EQ(battle()->battleGetRound(), 2);
	EXPECT_EQ(battle()->battleGetAttackLuck(source, target, false), 2);
	const auto baselineAllyLuck = battle()->battleGetAttackLuck(ally, target, false) - 2;
	ASSERT_TRUE(strike(source));
	ASSERT_FALSE(server.attacks.empty());
	ASSERT_TRUE(server.attacks.back().luckSerendipityState);
	EXPECT_TRUE(server.attacks.back().luckSerendipityState->firstAttackUsed);
	EXPECT_EQ(battle()->battleGetAttackLuck(ally, target, false), baselineAllyLuck);
	EXPECT_TRUE(battle()->getLuckSerendipityState(BattleSide::DEFENDER).availableAt(2));
	advanceRound();
	EXPECT_TRUE(battle()->getLuckSerendipityState(BattleSide::ATTACKER).availableAt(3));
}

TEST_F(NewHorizonsLuckSerendipityTest, ActualPositiveLuckInPreviousRoundPreventsBonus)
{
	goodChance = 100;
	ASSERT_NO_FATAL_FAILURE(prepare());
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::LUCK, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(strike(source));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_TRUE(server.attacks.back().lucky());
	EXPECT_TRUE(battle()->getLuckSerendipityState(BattleSide::ATTACKER).currentRoundPositiveLuck);
	advanceRound();
	EXPECT_TRUE(battle()->getLuckSerendipityState(BattleSide::ATTACKER).previousRoundPositiveLuck);
	EXPECT_FALSE(battle()->getLuckSerendipityState(BattleSide::ATTACKER).availableAt(2));
	EXPECT_EQ(battle()->battleGetAttackLuck(source, target, false), 1);
	advanceRound();
	EXPECT_TRUE(battle()->getLuckSerendipityState(BattleSide::ATTACKER).availableAt(3));
}

TEST_F(NewHorizonsLuckSerendipityTest, NoLuckSuppressionStillConsumesFirstAttack)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	advanceRound();
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::NO_LUCK, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(battle()->battleGetAttackLuck(source, target, false), 0);
	ASSERT_TRUE(strike(source));
	EXPECT_TRUE(battle()->getLuckSerendipityState(BattleSide::ATTACKER).firstAttackUsed);
	EXPECT_FALSE(battle()->getLuckSerendipityState(BattleSide::ATTACKER).currentRoundPositiveLuck);
}

TEST_F(NewHorizonsLuckSerendipityTest, UnselectedPerkLeavesRoundTwoLuckUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	advanceRound();
	EXPECT_EQ(battle()->battleGetAttackLuck(source, target, false), 0);
	ASSERT_TRUE(strike(source));
	EXPECT_EQ(battle()->getLuckSerendipityState(BattleSide::ATTACKER), LuckSerendipityState{});
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_FALSE(server.attacks.back().luckSerendipityState);
}

TEST_F(NewHorizonsLuckSerendipityTest, NativeSavedSideAndBattlePreserveRoundHistory)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	advanceRound();
	ASSERT_TRUE(strike(source));
	const auto expected = battle()->getLuckSerendipityState(BattleSide::ATTACKER);
	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	EXPECT_EQ(restored->getLuckSerendipityState(BattleSide::ATTACKER), expected);
	const auto oldVersion = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_LUCK_SERENDIPITY) - 1);
	CMemorySerializer older;
	older.oser.version = oldVersion;
	EXPECT_THROW(older.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(older.extractBuffer().empty());
}

TEST_F(NewHorizonsLuckSerendipityTest, DetachedCandidateAndReplayConsumeOnlyTheirOwnHistoryWithoutRng)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	advanceRound();
	activate(source);
	const auto liveHealth = target->getAvailableHealth();
	CMemorySerializer rngBefore;
	gameHandler->randomizer->serialize(rngBefore.oser);
	const auto rngBytes = rngBefore.extractBuffer();
	auto environment = std::make_shared<SerendipityEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	HypotheticBattle sibling(environment.get(), model);
	DamageCache cache;
	const auto prediction = AttackPossibility::evaluate(BattleAttackInfo(source, target, 0, false),
		source->getPosition(), cache, model);
	ASSERT_NE(prediction.effectPreview, nullptr);
	EXPECT_TRUE(prediction.effectPreview->getLuckSerendipityState(BattleSide::ATTACKER).firstAttackUsed);
	EXPECT_FALSE(prediction.effectPreview->getLuckSerendipityState(BattleSide::ATTACKER).currentRoundPositiveLuck);
	ASSERT_FALSE(prediction.fortuneStrikes.empty());
	EXPECT_EQ(prediction.fortuneStrikes.front().luckSerendipitySide, BattleSide::ATTACKER);
	EXPECT_TRUE(prediction.fortuneStrikes.front().luckSerendipityOrdinaryAttack);
	EXPECT_TRUE(model->getLuckSerendipityState(BattleSide::ATTACKER).availableAt(2));
	BattleExchangeVariant exchange;
	exchange.trackAttack(prediction, model, cache);
	EXPECT_TRUE(model->getLuckSerendipityState(BattleSide::ATTACKER).firstAttackUsed);
	EXPECT_TRUE(sibling.getLuckSerendipityState(BattleSide::ATTACKER).availableAt(2));
	EXPECT_TRUE(battle()->getLuckSerendipityState(BattleSide::ATTACKER).availableAt(2));
	EXPECT_EQ(target->getAvailableHealth(), liveHealth);
	CMemorySerializer rngAfter;
	gameHandler->randomizer->serialize(rngAfter.oser);
	EXPECT_EQ(rngAfter.extractBuffer(), rngBytes);
	ASSERT_TRUE(strike(source));
	EXPECT_EQ(target->getAvailableHealth(), model->battleGetUnitByID(target->unitId())->getAvailableHealth());
	EXPECT_EQ(battle()->getLuckSerendipityState(BattleSide::ATTACKER), model->getLuckSerendipityState(BattleSide::ATTACKER));
}
