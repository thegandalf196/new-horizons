/*
 * NewHorizonsBattlefieldMasteryAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/BattleTestFixture.h"

#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameConstants.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusEnum.h"
#include "../../lib/callback/GameCallbackHolder.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/modding/CModHandler.h"
#include "../../server/CGameHandler.h"

namespace
{
constexpr auto battlecraftSkill = "new-horizons:battlecraft";
constexpr auto reservePerk = "new-horizons:battlecraft.reserve";
constexpr auto passingLinesPerk = "new-horizons:battlecraft.passingLines";
constexpr auto masteryPerk = "new-horizons:battlecraft.battlefieldMastery";

class BattlefieldMasteryAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit BattlefieldMasteryAIEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsBattlefieldMasteryAITest : public BattleTestFixture
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

	void selectExpertMastery(CGHeroInstance * hero)
	{
		const auto decoded = SecondarySkill::decode(battlecraftSkill);
		ASSERT_GE(decoded, 0);
		const auto skill = SecondarySkill(decoded);
		const auto acceptPerk = [this, hero](std::string_view perkId)
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
		};

		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerk(reservePerk);
		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerk(passingLinesPerk);
		hero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		acceptPerk(masteryPerk);
		ASSERT_EQ(newHorizonsBattlecraft::rank(hero), 3);
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
};
}

TEST_F(NewHorizonsBattlefieldMasteryAITest, WaitAndDefendAwardsStayInsideTheirDetachedBranches)
{
	startGame();
	selectExpertMastery(attackerSideHero);
	startBattle();
	removeStartingStacks();
	auto * first = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * second = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(leftHex - 4), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	ASSERT_NE(enemy, nullptr);
	beginCombat();

	BattlefieldMasteryAIEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto waitBranch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto siblingBranch = std::make_shared<HypotheticBattle>(&environment, parent);

	waitBranch->makeWait(first);
	const auto projectedFirst = waitBranch->getForUpdate(first->unitId());
	ASSERT_NE(projectedFirst, nullptr);
	EXPECT_TRUE(projectedFirst->battlecraftWaitMasteryDoubled);
	EXPECT_EQ(waitBranch->getBattlecraftMasteryAwardRound(BattleSide::ATTACKER), 1);
	EXPECT_EQ(newHorizonsBattlecraft::waitDamagePercent(attackerSideHero, projectedFirst.get()), 30);
	EXPECT_FALSE(parent->getForUpdate(first->unitId())->battlecraftWaitMasteryDoubled);
	EXPECT_EQ(parent->getBattlecraftMasteryAwardRound(BattleSide::ATTACKER), -1);
	EXPECT_FALSE(first->battlecraftWaitMasteryDoubled);
	EXPECT_EQ(battle()->getBattlecraftMasteryAwardRound(BattleSide::ATTACKER), -1);

	projectedFirst->afterAttack(false, false, true);
	EXPECT_FALSE(projectedFirst->battlecraftWaitMasteryDoubled);
	EXPECT_EQ(waitBranch->getBattlecraftMasteryAwardRound(BattleSide::ATTACKER), 1)
		<< "Consuming the projected one-shot effect does not reopen the side opportunity";
	waitBranch->makeWait(second);
	EXPECT_FALSE(waitBranch->getForUpdate(second->unitId())->battlecraftWaitMasteryDoubled);
	EXPECT_EQ(waitBranch->getBattlecraftMasteryAwardRound(BattleSide::ATTACKER), 1);

	siblingBranch->makeWait(second);
	EXPECT_TRUE(siblingBranch->getForUpdate(second->unitId())->battlecraftWaitMasteryDoubled)
		<< "A sibling forecast starts from its own copy of the source award state";
	EXPECT_FALSE(second->battlecraftWaitMasteryDoubled);
	EXPECT_EQ(parent->getBattlecraftMasteryAwardRound(BattleSide::ATTACKER), -1);

	auto defendBranch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto projectedDefender = defendBranch->getForUpdate(second->unitId());
	ASSERT_NE(projectedDefender, nullptr);
	projectedDefender->defending = true;
	projectedDefender->addUnitBonus(std::vector<Bonus>{
		Bonus(BonusDuration::STACK_GETS_TURN, BonusType::UNIT_DEFENDING, BonusSource::OTHER, 0, BonusSourceID())});
	const auto defendSide = projectedDefender->unitSide();
	const auto round = defendBranch->getRound();
	ASSERT_TRUE(newHorizonsBattlecraft::canAwardBattlefieldMastery(attackerSideHero, projectedDefender.get(),
		round, defendBranch->getBattlecraftMasteryAwardRound(defendSide), BattlecraftMasteryAction::DEFEND));
	defendBranch->awardBattlecraftMastery(defendSide, projectedDefender->unitId(), round,
		BattlecraftMasteryAction::DEFEND);
	EXPECT_TRUE(projectedDefender->battlecraftDefendMasteryDoubled);
	EXPECT_EQ(newHorizonsBattlecraft::defendReductionPercent(attackerSideHero, projectedDefender.get()), 30);
	EXPECT_EQ(defendBranch->getBattlecraftMasteryAwardRound(BattleSide::ATTACKER), 1);
	EXPECT_FALSE(second->battlecraftDefendMasteryDoubled);
	EXPECT_EQ(battle()->getBattlecraftMasteryAwardRound(BattleSide::ATTACKER), -1);
	EXPECT_FALSE(enemy->battlecraftWaitMasteryDoubled);
}
