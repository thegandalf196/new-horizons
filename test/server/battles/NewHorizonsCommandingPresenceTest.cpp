/*
 * NewHorizonsCommandingPresenceTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{
constexpr auto commandSkill = "new-horizons:command";
constexpr auto basicCommandPerk = "new-horizons:command.aggressiveCommander";
constexpr auto commandingPresence = "new-horizons:command.commandingPresence";

class CommandingPresenceEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit CommandingPresenceEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsCommandingPresenceTest : public HeroCommandFixture
{
protected:
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
			if(perk["id"].String() == commandingPresence)
			{
				perk["effect"]["status"].String() = "active";
				found = true;
			}
		}
		if(!found)
			throw std::runtime_error("Missing Commanding Presence registry entry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perks));
	}

	bool offerContains(CGHeroInstance * hero, const std::string & perkId)
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
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

	void selectCommandingPresence(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(commandSkill);
		ASSERT_GE(decoded, 0);
		const auto skill = SecondarySkill(decoded);
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_FALSE(offerContains(hero, commandingPresence))
			<< "Commanding Presence is unavailable at Basic Command";
		acceptPerk(hero, basicCommandPerk);
		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(offerContains(hero, commandingPresence));
		acceptPerk(hero, commandingPresence);
	}

	void preparePresenceBattle()
	{
		startGame();
		selectCommandingPresence(attackerSideHero);
		startBattle();
		clearStartingUnits();
	}

	void clearStartingUnits()
	{
		BattleUnitsChanged changes;
		changes.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			changes.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(changes);
	}

	void beginPresenceCombat(const CStack * activeStack)
	{
		beginCombat();
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = activeStack->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
	}

	void giveNegativeMorale(CStack * stack)
	{
		stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::MORALE, BonusSource::OTHER, -100, BonusSourceID()));
	}

	int expectedNegativeMorale() const
	{
		return -static_cast<int>(LIBRARY->engineSettings()->getVector(
			EGameSettings::COMBAT_BAD_MORALE_CHANCE).size());
	}

	bool issue(HeroCommand command)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeHeroCommand(BattleSide::ATTACKER, command));
	}

	bool issueTargeted(HeroCommand command, const CStack * target)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, command, target->unitId()));
	}

	bool issueProtect(const CStack * protector, const CStack * ward)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
				protector->unitId(), ward->unitId()));
	}

	bool act(const CStack * stack, const BattleAction & action)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(stack), action);
	}

};
}

TEST_F(NewHorizonsCommandingPresenceTest, AdvancedPerkRequiresLegalBasicThenAdvancedOffers)
{
	startGame();
	const int decoded = SecondarySkill::decode(commandSkill);
	ASSERT_GE(decoded, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(offerContains(attackerSideHero, commandingPresence));
	acceptPerk(attackerSideHero, basicCommandPerk);

	attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	EXPECT_TRUE(offerContains(attackerSideHero, commandingPresence));
	acceptPerk(attackerSideHero, commandingPresence);
	EXPECT_TRUE(attackerSideHero->hasActivePerk(commandSkill, commandingPresence));
}

TEST_F(NewHorizonsCommandingPresenceTest, ChargeBenefitEndsOnConsumptionAndIsBranchLocal)
{
	preparePresenceBattle();
	auto * charger = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(89), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 100);
	auto * otherFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 100);
	beginPresenceCombat(charger);
	giveNegativeMorale(charger);
	giveNegativeMorale(otherFriendly);
	giveNegativeMorale(enemy);
	ASSERT_EQ(battle()->battleGetMorale(charger), expectedNegativeMorale());
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(battle()->battleGetMorale(charger), 0);
	EXPECT_EQ(battle()->battleGetMorale(otherFriendly), 0);
	EXPECT_EQ(battle()->battleGetMorale(enemy), expectedNegativeMorale())
		<< "An enemy unit is not protected by this hero's Order or perk";

	CommandingPresenceEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto projected = branch->getForUpdate(charger->unitId());
	ASSERT_EQ(branch->battleGetMorale(projected.get()), 0);
	auto branchOrder = branch->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(branchOrder);
	branchOrder->consumedUnitIds.push_back(charger->unitId());
	branch->setHeroOrderState(BattleSide::ATTACKER, branchOrder);
	EXPECT_EQ(branch->battleGetMorale(branch->getForUpdate(charger->unitId()).get()), expectedNegativeMorale());
	EXPECT_EQ(parent->battleGetMorale(parent->getForUpdate(charger->unitId()).get()), 0);
	EXPECT_EQ(sibling->battleGetMorale(sibling->getForUpdate(charger->unitId()).get()), 0);
	EXPECT_EQ(battle()->battleGetMorale(charger), 0);

	auto control = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	charger->addNewBonus(control);
	ASSERT_EQ(battle()->battleGetOwnerHero(charger), defenderSideHero);
	EXPECT_EQ(battle()->battleGetMorale(charger), expectedNegativeMorale())
		<< "An old-side Order and its hero perk do not follow a unit after hostile control changes";
	charger->removeBonus(control);
	EXPECT_EQ(battle()->battleGetMorale(charger), 0);

	blockRetaliation(charger);
	blockRetaliation(enemy);
	battle()->activeStack = charger->unitId();
	const auto chargeAttack = BattleAction::makeMeleeAttack(charger, enemy, BattleHex(92), false);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), chargeAttack));
	const auto consumed = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(consumed);
	EXPECT_TRUE(consumed->containsConsumed(charger->unitId()));
	EXPECT_EQ(battle()->battleGetMorale(charger), expectedNegativeMorale())
		<< "The floor ends as soon as this stack spends its one-shot Charge benefit";
	EXPECT_EQ(battle()->battleGetMorale(otherFriendly), 0)
		<< "Other unconsumed recipients keep their own Charge benefit";
}

TEST_F(NewHorizonsCommandingPresenceTest, FocusFireCoversItsShooterCohortButNotMeleeStacks)
{
	preparePresenceBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 100);
	auto * melee = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(14, 5), 100);
	beginPresenceCombat(shooter);
	giveNegativeMorale(shooter);
	giveNegativeMorale(melee);
	EXPECT_EQ(battle()->battleGetMorale(shooter), expectedNegativeMorale());
	EXPECT_EQ(battle()->battleGetMorale(melee), expectedNegativeMorale());
	ASSERT_TRUE(battle()->battleCanShoot(shooter, target->getPosition()));
	ASSERT_TRUE(issueTargeted(HeroCommand::FOCUS_FIRE, target));
	EXPECT_EQ(battle()->battleGetMorale(shooter), 0);
	EXPECT_EQ(battle()->battleGetMorale(melee), expectedNegativeMorale());

	auto control = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	target->addNewBonus(control);
	EXPECT_EQ(battle()->battleGetMorale(shooter), expectedNegativeMorale())
		<< "The shooter cohort loses its floor when the marked enemy is no longer hostile";
}

TEST_F(NewHorizonsCommandingPresenceTest, CombinedArmsFocusFireDropsSpellLikeMeleeFromItsSavedCohort)
{
	startGame();
	const int decoded = SecondarySkill::decode(commandSkill);
	ASSERT_GE(decoded, 0);
	const auto skill = SecondarySkill(decoded);
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	acceptPerk(attackerSideHero, basicCommandPerk);
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	acceptPerk(attackerSideHero, "new-horizons:command.combinedArms");
	ASSERT_TRUE(attackerSideHero->hasActivePerk(commandSkill, "new-horizons:command.combinedArms"));

	startBattle();
	clearStartingUnits();
	auto * melee = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(71), 100);
	beginPresenceCombat(melee);
	ASSERT_TRUE(issueTargeted(HeroCommand::FOCUS_FIRE, target));
	const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	const auto focus = battle()->battleGetFocusFireState(BattleSide::ATTACKER);
	ASSERT_TRUE(focus);
	ASSERT_TRUE(std::binary_search(focus->recipientUnitIds.begin(), focus->recipientUnitIds.end(), melee->unitId()));
	EXPECT_TRUE(battle()->battleOrderBenefitAppliesTo(*state, BattleSide::ATTACKER, melee));

	melee->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SPELL_LIKE_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_FALSE(battle()->battleOrderBenefitAppliesTo(*state, BattleSide::ATTACKER, melee))
		<< "Combined Arms does not preserve Focus Fire eligibility for a spell-like attack";
}

TEST_F(NewHorizonsCommandingPresenceTest, RiposteAndBraceCoverLivingFriendlyStacksUntilRoundExpiry)
{
	preparePresenceBattle();
	auto * first = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 100);
	auto * second = addStack(BattleSide::ATTACKER, creatureByName("core:sprite"), BattleHex(54), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(71), 100);
	giveNegativeMorale(enemy);
	beginPresenceCombat(first);
	giveNegativeMorale(first);
	giveNegativeMorale(second);
	EXPECT_EQ(battle()->battleGetMorale(first), expectedNegativeMorale());
	EXPECT_EQ(battle()->battleGetMorale(second), expectedNegativeMorale());
	EXPECT_EQ(battle()->battleGetMorale(enemy), expectedNegativeMorale());
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	EXPECT_EQ(battle()->battleGetMorale(first), 0);
	EXPECT_EQ(battle()->battleGetMorale(second), 0);
	EXPECT_EQ(battle()->battleGetMorale(enemy), expectedNegativeMorale());

	advanceRound();
	EXPECT_EQ(battle()->battleGetMorale(first), expectedNegativeMorale());
	ASSERT_TRUE(issue(HeroCommand::BRACE));
	EXPECT_EQ(battle()->battleGetMorale(first), 0);
	EXPECT_EQ(battle()->battleGetMorale(second), 0);
	advanceRound();
	EXPECT_EQ(battle()->battleGetMorale(first), expectedNegativeMorale())
		<< "Brace's benefit, like Riposte's, expires with its round-scoped Order";
}

TEST_F(NewHorizonsCommandingPresenceTest, HoldOnlyProtectsAnchoredStacksAndEndsWhenAnchorBreaks)
{
	preparePresenceBattle();
	auto * anchored = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(71), 100);
	ASSERT_NE(enemy, nullptr);
	beginPresenceCombat(anchored);
	giveNegativeMorale(anchored);
	EXPECT_EQ(battle()->battleGetMorale(anchored), expectedNegativeMorale());
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	EXPECT_EQ(battle()->battleGetMorale(anchored), 0);

	auto * lateArrival = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(54), 100);
	giveNegativeMorale(lateArrival);
	EXPECT_EQ(battle()->battleGetMorale(lateArrival), expectedNegativeMorale());

	BattleStackMoved moved;
	moved.battleID = BattleID(0);
	moved.stack = anchored->unitId();
	moved.tilesToMove.insert(BattleHex(72));
	gameHandler->sendAndApply(moved);
	const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	EXPECT_TRUE(state->containsHoldBroken(anchored->unitId()));
	EXPECT_EQ(battle()->battleGetMorale(anchored), expectedNegativeMorale())
		<< "Returning to the original hex cannot restore a broken Hold benefit";
}

TEST_F(NewHorizonsCommandingPresenceTest, ProtectIsPairScopedAndEndsOnSeparationOrExhaustion)
{
	preparePresenceBattle();
	auto * firstProtector = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
	auto * firstWard = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(71), 100);
	auto * firstEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(72), 100);
	auto * secondProtector = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(54), 100);
	auto * secondWard = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(55), 100);
	auto * secondEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(56), 100);
	ASSERT_NE(firstEnemy, nullptr);
	beginPresenceCombat(firstProtector);
	for(auto * stack : {firstProtector, firstWard, secondProtector, secondWard})
		giveNegativeMorale(stack);
	EXPECT_EQ(battle()->battleGetMorale(firstProtector), expectedNegativeMorale());
	EXPECT_EQ(battle()->battleGetMorale(firstWard), expectedNegativeMorale());
	EXPECT_EQ(battle()->battleGetMorale(secondProtector), expectedNegativeMorale());
	ASSERT_TRUE(issueProtect(firstProtector, firstWard));
	EXPECT_EQ(battle()->battleGetMorale(firstProtector), 0);
	EXPECT_EQ(battle()->battleGetMorale(firstWard), 0);
	EXPECT_EQ(battle()->battleGetMorale(secondProtector), expectedNegativeMorale());

	BattleStackMoved separated;
	separated.battleID = BattleID(0);
	separated.stack = firstWard->unitId();
	separated.tilesToMove.insert(BattleHex(74));
	gameHandler->sendAndApply(separated);
	ASSERT_TRUE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER)->protectBroken);
	EXPECT_EQ(battle()->battleGetMorale(firstProtector), expectedNegativeMorale());
	EXPECT_EQ(battle()->battleGetMorale(firstWard), expectedNegativeMorale());

	advanceRound();
	ASSERT_TRUE(issueProtect(secondProtector, secondWard));
	blockRetaliation(secondProtector);
	blockRetaliation(secondWard);
	ASSERT_TRUE(attack(secondEnemy, secondWard->getPosition()));
	const auto exhausted = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(exhausted);
	EXPECT_EQ(exhausted->protectInterceptionsConsumed, exhausted->protectInterceptionLimit);
	EXPECT_EQ(battle()->battleGetMorale(secondProtector), expectedNegativeMorale());
	EXPECT_EQ(battle()->battleGetMorale(secondWard), expectedNegativeMorale())
		<< "The paired recipients lose the floor after Protect spends its final interception";
}

TEST_F(NewHorizonsCommandingPresenceTest, FlankCoversMeleeCapableStacksAndRequiresAHostileTarget)
{
	preparePresenceBattle();
	auto * melee = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 100);
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(71), 100);
	beginPresenceCombat(melee);
	giveNegativeMorale(melee);
	giveNegativeMorale(shooter);
	EXPECT_EQ(battle()->battleGetMorale(melee), expectedNegativeMorale());
	EXPECT_EQ(battle()->battleGetMorale(shooter), expectedNegativeMorale());
	ASSERT_TRUE(issueTargeted(HeroCommand::FLANK, target));
	EXPECT_EQ(battle()->battleGetMorale(melee), 0);
	EXPECT_EQ(battle()->battleGetMorale(shooter), 0)
		<< "A shooter is also a melee attacker and receives Flank's melee benefit";

	auto control = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	target->addNewBonus(control);
	EXPECT_EQ(battle()->battleGetMorale(melee), expectedNegativeMorale())
		<< "A Flank order no longer benefits melee stacks after its hostile target ceases to be hostile";
	EXPECT_EQ(battle()->battleGetMorale(shooter), expectedNegativeMorale());
}

TEST_F(NewHorizonsCommandingPresenceTest, SecondWindFloorEndsWithItsExtraActivation)
{
	preparePresenceBattle();
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
	auto * other = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(54), 100);
	addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(71), 100);
	beginPresenceCombat(target);
	giveNegativeMorale(target);
	giveNegativeMorale(other);
	target->movedThisRound = true;
	EXPECT_EQ(battle()->battleGetMorale(target), expectedNegativeMorale());
	EXPECT_EQ(battle()->battleGetMorale(other), expectedNegativeMorale());
	ASSERT_TRUE(issueTargeted(HeroCommand::SECOND_WIND, target));
	const auto active = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(active);
	EXPECT_TRUE(active->secondWindActive);
	EXPECT_EQ(battle()->battleActiveUnit(), target);
	EXPECT_EQ(battle()->battleGetMorale(target), 0);
	EXPECT_EQ(battle()->battleGetMorale(other), expectedNegativeMorale())
		<< "Second Wind affects only its designated extra-activation recipient";

	ASSERT_TRUE(act(target, BattleAction::makeDefend(target)));
	const auto spent = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(spent);
	EXPECT_FALSE(spent->secondWindActive);
	EXPECT_EQ(battle()->battleGetMorale(target), expectedNegativeMorale())
		<< "The floor ends as the extra activation is spent, not at round end";
}
