/*
 * NewHorizonsDoubleCommandTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "BattleStartSnapshotFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/HeroActionAllowanceState.h"
#include "../../../lib/battle/SideInBattle.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../AI/BattleAI/HypotheticBattle.h"

namespace
{
constexpr auto commandSkill = "new-horizons:command";
constexpr auto doubleCommandPerk = "new-horizons:command.doubleCommand";
constexpr auto basicCommandPerk = "new-horizons:command.aggressiveCommander";
constexpr auto advancedCommandPerk = "new-horizons:command.veteranCommander";

class DoubleCommandEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit DoubleCommandEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsDoubleCommandTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		useRevisedOrders = true;
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
				// After activation, exercise production registration without a test override.
				if(perk["effect"]["status"].String() == "active")
					return;
				perk["effect"]["status"].String() = "active";
				found = true;
			}
		}
		if(!found)
			throw std::runtime_error("Missing Double Command registry entry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perks));
	}

	bool offerContains(CGHeroInstance * hero, const std::string & perkId)
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		const auto weightContext = hero->getPerkOfferWeightContext();
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed, weightContext);
			for(const auto & offer : offers)
				if(offer.selection.skillId == commandSkill && offer.selection.perkId == perkId)
					return true;
		}
		return false;
	}

	void acceptPerk(CGHeroInstance * hero, const std::string & perkId)
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		const auto weightContext = hero->getPerkOfferWeightContext();
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed, weightContext);
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
		hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:orrin")));
		const int decoded = SecondarySkill::decode(commandSkill);
		ASSERT_GE(decoded, 0);
		const auto skill = SecondarySkill(decoded);
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		EXPECT_FALSE(offerContains(hero, doubleCommandPerk))
			<< "Double Command must not be offered below Expert Command";
		acceptPerk(hero, basicCommandPerk);

		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		EXPECT_FALSE(offerContains(hero, doubleCommandPerk))
			<< "Double Command must not be offered at Advanced Command";
		acceptPerk(hero, advancedCommandPerk);

		hero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(offerContains(hero, doubleCommandPerk));
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

	void beginWith(CStack * activeStack)
	{
		beginCombat();
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = activeStack->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
		ASSERT_EQ(battle()->battleActiveUnit(), activeStack);
	}

	void grantOrderAllowance(BattleSide side)
	{
		battle()->getSide(side).heroActionAllowances.grantAllowance(
			HeroActionAllowanceState::AllowanceKind::ORDER,
			HeroActionAllowanceState::GrantSource::PERK,
			battle()->getRound());
	}

	bool issue(BattleSide side, HeroCommand command)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(side),
			BattleAction::makeHeroCommand(side, command));
	}

	bool issueTargeted(BattleSide side, HeroCommand command, uint32_t targetId)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(side),
			BattleAction::makeTargetedHeroCommand(side, command, targetId));
	}

	bool issueProtect(const CStack * protector, const CStack * ward)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
				protector->unitId(), ward->unitId()));
	}

	void startGameWithPerk(bool spellbook = false)
	{
		startGame();
		selectDoubleCommand(attackerSideHero);
		if(spellbook)
		{
			giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
			setTestSpellPointTotal(attackerSideHero, 100);
		}
	}

	void startCommandBattle(CStack *& active, CStack *& target, CStack *& enemy)
	{
		startBattle();
		clearStartingUnits();
		active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 100);
		target = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(71), 100);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(target, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginWith(active);
	}
};
}

TEST_F(NewHorizonsDoubleCommandTest, ExpertPerkUsesTheOrdinaryRankGatedOfferPath)
{
	startGame();
	selectDoubleCommand(attackerSideHero);

	const int decoded = SecondarySkill::decode(commandSkill);
	ASSERT_GE(decoded, 0);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill(decoded)), MasteryLevel::EXPERT);
	EXPECT_TRUE(attackerSideHero->hasActivePerk(commandSkill, doubleCommandPerk));
}

TEST_F(NewHorizonsDoubleCommandTest, MasterCommanderPaidOrderCannotCreateAnImmediateFollowup)
{
	startGameWithPerk(true);
	CStack * active = nullptr, * shooter = nullptr, * enemy = nullptr;
	startCommandBattle(active, shooter, enemy);
	const auto resources = battle()->getCommandResources(BattleSide::ATTACKER);
	const auto serial = battle()->getActivationSerial();
	ASSERT_TRUE(issue(BattleSide::ATTACKER, HeroCommand::CHARGE));
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER).remaining, resources.remaining - 1);
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->getDoubleCommandState(BattleSide::ATTACKER).phase, DoubleCommandState::Phase::NONE);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(1).heroActions, 0u);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(1).orderActions, 0u);
	EXPECT_EQ(battle()->battleActiveUnit(), active);
	EXPECT_EQ(battle()->getActivationSerial(), serial);
	const auto orders = battle()->getHeroOrderStates(BattleSide::ATTACKER);
	EXPECT_FALSE(issue(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
	EXPECT_EQ(battle()->getHeroOrderStates(BattleSide::ATTACKER), orders);
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER).remaining, resources.remaining - 1);
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(active)));
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
}

TEST_F(NewHorizonsDoubleCommandTest, CurrentDescriptorKeepsPaidResourcesWithoutRetiredPendingChoice)
{
	startGameWithPerk();
	CStack * active = nullptr, * shooter = nullptr, * enemy = nullptr;
	startCommandBattle(active, shooter, enemy);
	ASSERT_TRUE(issue(BattleSide::ATTACKER, HeroCommand::CHARGE));
	const auto resources = battle()->getCommandResources(BattleSide::ATTACKER);
	const auto snapshot = battleStartFixture::snapshot(*battle(), gameState().get());
	ASSERT_NE(snapshot, nullptr);
	EXPECT_EQ(snapshot->getCommandResources(BattleSide::ATTACKER), resources);
	EXPECT_EQ(snapshot->getDoubleCommandState(BattleSide::ATTACKER).phase, DoubleCommandState::Phase::NONE);
	EXPECT_EQ(snapshot->getHeroActionAllowances(BattleSide::ATTACKER), battle()->getHeroActionAllowances(BattleSide::ATTACKER));
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_MULTIPLE_ORDERS;
	EXPECT_THROW(old.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

TEST_F(NewHorizonsDoubleCommandTest, IntrinsicSecondWindStartsWithoutAnAdditionalOrderDecision)
{
	startGameWithPerk();
	CStack * active = nullptr, * target = nullptr, * enemy = nullptr;
	startCommandBattle(active, target, enemy);
	target->movedThisRound = true;
	const auto before = battle()->getCommandResources(BattleSide::ATTACKER);
	EXPECT_FALSE(issueTargeted(BattleSide::ATTACKER, HeroCommand::SECOND_WIND,
		static_cast<uint32_t>(std::numeric_limits<int32_t>::max())));
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER), before);
	ASSERT_TRUE(issueTargeted(BattleSide::ATTACKER, HeroCommand::SECOND_WIND, target->unitId()));
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER).remaining, before.remaining - 1);
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleActiveUnit(), target);
	const auto order = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
	ASSERT_TRUE(order);
	EXPECT_TRUE(order->secondWindActive);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(target)));
	EXPECT_FALSE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND)->secondWindActive);
	EXPECT_FALSE(battle()->getDoubleCommandState(BattleSide::ATTACKER).used);
}
