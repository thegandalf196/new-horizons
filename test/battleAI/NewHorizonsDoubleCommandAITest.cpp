/*
 * NewHorizonsDoubleCommandAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/HeroActionAllowanceState.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"

namespace
{
constexpr auto commandSkill = "new-horizons:command";
constexpr auto doubleCommandPerk = "new-horizons:command.doubleCommand";
constexpr auto basicCommandPerk = "new-horizons:command.aggressiveCommander";
constexpr auto advancedCommandPerk = "new-horizons:command.veteranCommander";

class DoubleCommandAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit DoubleCommandAIEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class RecordingDoubleCommandCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> heroActions;
	std::vector<BattleAction> creatureActions;

	RecordingDoubleCommandCallback() : CBattleCallback(PlayerColor(0), nullptr) {}

	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		heroActions.push_back(action);
	}

	void battleMakeUnitAction(const BattleID &, const BattleAction & action) override
	{
		creatureActions.push_back(action);
	}
};
}

class NewHorizonsDoubleCommandAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<DoubleCommandAIEnvironment> environment;
	std::shared_ptr<RecordingDoubleCommandCallback> callback;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		bool found = false;
		for(auto & perk : perks["skills"][commandSkill]["perks"].Vector())
		{
			if(perk["id"].String() == doubleCommandPerk)
			{
				perk["effect"]["status"].String() = "active";
				found = true;
			}
		}
		if(!found)
			throw std::runtime_error("Missing Double Command registry entry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perks));
	}

	void acceptPerk(CGHeroInstance * hero, const std::string & perkId)
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
			{
				if(offers[choice].selection.skillId == commandSkill && offers[choice].selection.perkId == perkId)
				{
					gameHandler->levelUpHero(hero, offers, choice, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(commandSkill, perkId));
					return;
				}
			}
		}
		FAIL() << "No legal perk offer for " << perkId;
	}

	void selectDoubleCommand(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(commandSkill);
		ASSERT_GE(decoded, 0);
		const auto skill = SecondarySkill(decoded);
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, basicCommandPerk);
		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, advancedCommandPerk);
		hero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, doubleCommandPerk);
		EXPECT_TRUE(hero->hasActivePerk(commandSkill, basicCommandPerk));
		EXPECT_TRUE(hero->hasActivePerk(commandSkill, advancedCommandPerk));
	}

	void clearStartingUnits()
	{
		BattleUnitsChanged changes;
		changes.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			changes.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(changes);
	}

	void spendDefenderHeroAllowanceAsSpell()
	{
		const auto round = battle()->getRound();
		auto ledgerCopy = battle()->getHeroActionAllowances(BattleSide::DEFENDER);
		const auto selection = ledgerCopy.eligibleAllowance(
			HeroActionAllowanceState::ActionKind::SPELL, round);
		ASSERT_TRUE(selection);
		EXPECT_EQ(selection->allowance, HeroActionAllowanceState::AllowanceKind::HERO);
		EXPECT_EQ(selection->source, HeroActionAllowanceState::GrantSource::ROUND);
		uint8_t metamagicUsesConsumed = 0;
		uint8_t metamagicPendingCount = 0;
		bool metamagicGrandUsed = false;
		const auto receipt = HeroSpellAllowanceTransition::commitAcceptedCast(ledgerCopy,
			selection->grantId, round, false, false, 0, false, metamagicUsesConsumed,
			metamagicPendingCount, metamagicGrandUsed, 0);
		ASSERT_TRUE(receipt);
		EXPECT_EQ(receipt->receipt.action, HeroActionAllowanceState::ActionKind::SPELL);
		EXPECT_EQ(receipt->receipt.allowance, HeroActionAllowanceState::AllowanceKind::HERO);
		EXPECT_EQ(receipt->receipt.round, round);
		EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER)
			.remainingCounts(round).heroActions, 1u)
			<< "Receipt inspection must not mutate the authoritative battle budget";

		BattleAction spell;
		spell.actionType = EActionType::HERO_SPELL;
		spell.side = BattleSide::DEFENDER;
		spell.spell = SpellID::HASTE;
		spell.aimToUnit(enemy);
		const auto startedActions = server.startedActions.size();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(BattleSide::DEFENDER), spell));
		ASSERT_EQ(server.startedActions.size(), startedActions + 1);
		EXPECT_EQ(server.startedActions.back().ba.actionType, EActionType::HERO_SPELL);
		EXPECT_EQ(server.startedActions.back().ba.side, BattleSide::DEFENDER);
		EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER)
			.remainingCounts(round).heroActions, 0u);
	}

	bool issue(HeroCommand command)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeHeroCommand(BattleSide::ATTACKER, command));
	}

	void preparePendingChoice()
	{
		startGame();
		selectDoubleCommand(attackerSideHero);
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		defenderSideHero->addSpellToSpellbook(SpellID::HASTE);
		setTestSpellPointTotal(defenderSideHero, 100);
		startBattle();
		clearStartingUnits();
		active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(71), 100);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();

		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = enemy->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		ASSERT_EQ(battle()->battleActiveUnit(), enemy);
		spendDefenderHeroAllowanceAsSpell();
		activate.stack = active->unitId();
		gameHandler->sendAndApply(activate);
		ASSERT_EQ(battle()->battleActiveUnit(), active);

		ASSERT_TRUE(issue(HeroCommand::CHARGE));
		ASSERT_TRUE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
		ASSERT_EQ(battle()->getDoubleCommandState(BattleSide::ATTACKER).phase,
			DoubleCommandState::Phase::ORDER_REQUIRED);

		callback = std::make_shared<RecordingDoubleCommandCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<DoubleCommandAIEnvironment>(gameState());
	}
};

TEST_F(NewHorizonsDoubleCommandAITest, MandatoryChoiceSubmitsAndResolvesOneDistinctOrder)
{
	preparePendingChoice();
	const auto pending = battle()->getDoubleCommandState(BattleSide::ATTACKER);
	ASSERT_EQ(pending.firstOrder, HeroCommand::CHARGE);
	const auto pendingBudget = battle()->getHeroActionAllowances(BattleSide::ATTACKER)
		.remainingCounts(battle()->getRound());
	ASSERT_EQ(pendingBudget.heroActions, 0u);
	ASSERT_EQ(pendingBudget.orderActions, 1u);
	const auto startedActions = server.startedActions.size();

	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	ASSERT_TRUE(evaluator.canCastSpell())
		<< "The evaluator must recognize a pending mandatory continuation even though the Hero Action was spent";
	ASSERT_TRUE(evaluator.attemptCastingSpell(active, true))
		<< "The evaluator must submit the mandatory Order instead of declining after ordinary forecast evaluation";

	ASSERT_EQ(callback->heroActions.size(), 1u)
		<< "The pending Double Command must submit exactly one Order request";
	EXPECT_TRUE(callback->creatureActions.empty())
		<< "The pending choice must not submit a Creature Action";
	const auto & selected = callback->heroActions.front();
	ASSERT_EQ(selected.actionType, EActionType::HERO_COMMAND);
	ASSERT_TRUE(heroCommands::isActive(selected.command));
	EXPECT_NE(selected.command, pending.firstOrder);
	EXPECT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, selected.command)
		|| battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, selected.command));
	EXPECT_EQ(server.startedActions.size(), startedActions)
		<< "AI candidate evaluation is read-only until the selected Order reaches the server";

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), selected));
	ASSERT_EQ(server.startedActions.size(), startedActions + 1);
	EXPECT_EQ(server.startedActions.back().ba.actionType, EActionType::HERO_COMMAND);
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	const auto orders = battle()->battleGetHeroOrderStates(BattleSide::ATTACKER);
	ASSERT_EQ(orders.size(), 2u);
	EXPECT_EQ(orders[0].command, HeroCommand::CHARGE);
	EXPECT_EQ(orders[1].command, selected.command);
	EXPECT_NE(orders[0].command, orders[1].command);
	const auto resolvedBudget = battle()->getHeroActionAllowances(BattleSide::ATTACKER)
		.remainingCounts(battle()->getRound());
	EXPECT_EQ(resolvedBudget.heroActions, 0u);
	EXPECT_EQ(resolvedBudget.orderActions, 0u);
	const auto resolved = battle()->getDoubleCommandState(BattleSide::ATTACKER);
	EXPECT_TRUE(resolved.used);
	EXPECT_EQ(resolved.phase, DoubleCommandState::Phase::NONE);
}
