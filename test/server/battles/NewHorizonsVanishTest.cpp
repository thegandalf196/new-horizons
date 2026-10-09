/*
 * NewHorizonsVanishTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/BattleAI.h"
#include "../../../lib/battle/PossiblePlayerBattleAction.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsShroud.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/CHeroClass.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include <vcmi/Environment.h>

namespace
{
class VanishEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit VanishEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsVanishTest : public BattleTestFixture
{
protected:
	CStack * shooter = nullptr;
	CStack * target = nullptr;
	CStack * first = nullptr;
	CStack * second = nullptr;
	CStack * survivor = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}

	void select(CGHeroInstance * hero, const std::string & skillId, const std::string & perkId,
		MasteryLevel::Type rank)
	{
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skillId)), rank, ChangeValueMode::ABSOLUTE);
		const auto lookup = [hero](const std::string & id) { return hero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(lookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
				if(offers[choice].selection.skillId == skillId && offers[choice].selection.perkId == perkId)
				{
					gameHandler->levelUpHero(hero, offers, choice, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(skillId, perkId));
					return;
				}
		}
		FAIL() << "No legal offer for " << perkId;
	}

	void prepare(bool selected = true)
	{
		HeroTypeID dungeon = HeroTypeID::NONE;
		for(const auto id : LIBRARY->heroh->getDefaultAllowed())
		{
			const auto * hero = dynamic_cast<const CHero *>(id.toHeroType());
			if(hero && hero->heroClass && hero->heroClass->faction == FactionID::DUNGEON)
			{
				dungeon = id;
				break;
			}
		}
		ASSERT_NE(dungeon, HeroTypeID::NONE);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, dungeon, PlayerColor(0)).heroGarrison({{CreatureID(0), 1}})
			.hero({7, 7, 0}, HeroTypeID(0), PlayerColor(1)).heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			ASSERT_NE(hero, nullptr);
			for(const auto & bonus : hero->getHeroType()->specialty)
				hero->removeBonus(bonus);
			for(int i = 0; i < LIBRARY->skillh->size(); ++i)
				hero->setSecSkillLevel(SecondarySkill(i), 0, ChangeValueMode::ABSOLUTE);
			for(auto skill : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE, PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE})
				hero->setPrimarySkill(skill, 0, ChangeValueMode::ABSOLUTE);
		}
		select(attackerSideHero, std::string(newHorizonsShroud::SKILL_ID),
			std::string(newHorizonsShroud::BACKSTAB_PERK_ID), MasteryLevel::BASIC);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(newHorizonsShroud::SKILL_ID))),
			MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		select(attackerSideHero, std::string(newHorizonsShroud::SKILL_ID),
			std::string(newHorizonsShroud::NO_ESCAPE_PERK_ID), MasteryLevel::ADVANCED);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(newHorizonsShroud::SKILL_ID))),
			MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		if(selected)
			select(attackerSideHero, std::string(newHorizonsShroud::SKILL_ID),
				std::string(newHorizonsShroud::VANISH_PERK_ID), MasteryLevel::EXPERT);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(70), 100);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(94), 1);
		survivor = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(140), 1000);
	}

	CStack * contact(size_t ordinal)
	{
		size_t available = 0;
		for(const auto hex : target->getSurroundingHexes())
			if(hex.isAvailable() && !battle()->battleGetStackByPos(hex))
			{
				if(available++ == ordinal)
					return addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), hex, 100);
			}
		ADD_FAILURE() << "No legal adjacent contact";
		return nullptr;
	}

	void surround()
	{
		first = contact(0);
		second = contact(0);
		ASSERT_NE(first, nullptr);
		ASSERT_NE(second, nullptr);
	}

	void addFlanker()
	{
		for(const auto hex : target->getSurroundingHexes())
		{
			if(!hex.isAvailable() || battle()->battleGetStackByPos(hex))
				continue;
			auto * candidate = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), hex, 100);
			ASSERT_NE(candidate, nullptr);
			if(battle()->battleIsShroudFlankingAttack(BattleAttackInfo(candidate, target, 0, false)))
			{
				first = candidate;
				return;
			}
			BattleUnitsChanged remove;
			remove.battleID = BattleID(0);
			remove.changedStacks.emplace_back(candidate->unitId(), UnitChanges::EOperation::REMOVE);
			gameHandler->sendAndApply(remove);
		}
		FAIL() << "No rear-facing legal melee flank";
	}

	void activate(const CStack * actor)
	{
		beginCombat();
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = actor->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}

	bool hit(const CStack * actor, const CStack * victim, const BattleHex & from)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(actor),
			BattleAction::makeMeleeAttack(actor, victim->getPosition(), from));
	}

	BattleAttackInfo flank() const { return BattleAttackInfo(first, target, 0, false); }
};
}



TEST(NewHorizonsVanishRulesTest, HalfSpeedFloorsWithoutNegativeMovement)
{
	EXPECT_EQ(newHorizonsShroud::vanishMovementAllowance(-1), 0);
	EXPECT_EQ(newHorizonsShroud::vanishMovementAllowance(0), 0);
	EXPECT_EQ(newHorizonsShroud::vanishMovementAllowance(1), 0);
	EXPECT_EQ(newHorizonsShroud::vanishMovementAllowance(5), 2);
	EXPECT_EQ(newHorizonsShroud::vanishMovementAllowance(8), 4);
}

TEST_F(NewHorizonsVanishTest, ActualFlankKillKeepsMovementOnlyUIAndAIThenAcceptsMove)
{
	prepare();
	addFlanker();
	ASSERT_TRUE(newHorizonsShroud::hasVanish(attackerSideHero));
	forceMaximumDamage(first);
	activate(first);
	ASSERT_TRUE(battle()->battleIsShroudFlankingAttack(flank()));
	ASSERT_TRUE(hit(first, target, first->getPosition()));
	EXPECT_FALSE(target->alive());
	ASSERT_EQ(first->pursuitMovementRemaining, newHorizonsShroud::vanishMovementAllowance(first->getMovementRange(0)));
	ASSERT_GT(first->pursuitMovementRemaining, 0);
	EXPECT_EQ(battle()->getActiveStackID(), first->unitId());
	BattleClientInterfaceData clientData{};
	const auto actions = battle()->getClientActionsForStack(first, clientData);
	ASSERT_EQ(actions.size(), 1u);
	EXPECT_EQ(actions.front(), PossiblePlayerBattleAction::MOVE_STACK);
	EXPECT_FALSE(hit(first, survivor, first->getPosition()));
	auto callback = std::shared_ptr<CBattleInfoCallback>(battle(), [](CBattleInfoCallback *) {});
	const auto aiAction = CBattleAI::choosePursuitMovement(callback, first);
	ASSERT_EQ(aiAction.actionType, EActionType::WALK);
	const auto aiTarget = aiAction.getTarget(battle());
	ASSERT_EQ(aiTarget.size(), 1u);
	const auto aiPath = battle()->getPath(first->getPosition(), aiTarget.front().hexValue, first);
	EXPECT_GT(aiPath.second, 0);
	EXPECT_LE(aiPath.second, first->pursuitMovementRemaining);
	const auto oldPosition = first->getPosition();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(first),
		BattleAction::makeMove(first, target->getPosition())));
	EXPECT_NE(first->getPosition(), oldPosition);
	EXPECT_EQ(first->pursuitMovementRemaining, 0);
}

TEST_F(NewHorizonsVanishTest, DeclineConsumesSavedMovementWithoutCreatingAttack)
{
	prepare();
	addFlanker();
	activate(first);
	ASSERT_TRUE(hit(first, target, first->getPosition()));
	ASSERT_GT(first->pursuitMovementRemaining, 0);
	auto restored = first->acquireState();
	const auto saved = restored->save();
	restored->pursuitMovementRemaining = 0;
	ASSERT_NO_THROW(restored->load(saved));
	EXPECT_EQ(restored->pursuitMovementRemaining, first->pursuitMovementRemaining);
	const auto priorPosition = first->getPosition();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(first),
		BattleAction::makeNoAction(first)));
	EXPECT_EQ(first->pursuitMovementRemaining, 0);
	EXPECT_EQ(first->getPosition(), priorPosition);
}

TEST_F(NewHorizonsVanishTest, FullySpentApproachStillGrantsHalfPostAttackSpeed)
{
	prepare();
	addFlanker();
	const auto landing = first->getPosition();
	const auto speed = first->getMovementRange(0);
	BattleHex start = BattleHex::INVALID;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(index);
		if(!candidate.isAvailable() || battle()->battleGetStackByPos(candidate))
			continue;
		// Relocate the fixture actor itself: a projected unit alone leaves its
		// live counterpart occupying the landing endpoint in accessibility.
		first->setPosition(candidate);
		const auto path = battle()->getPath(candidate, landing, first);
		first->setPosition(landing);
		if(!path.first.empty() && path.second == speed)
		{
			start = candidate;
			break;
		}
	}
	ASSERT_TRUE(start.isAvailable());
	first->setPosition(start);
	activate(first);
	ASSERT_EQ(battle()->getPath(start, landing, first).second, speed);
	ASSERT_TRUE(hit(first, target, landing));
	EXPECT_FALSE(target->alive());
	EXPECT_EQ(first->pursuitMovementRemaining, newHorizonsShroud::vanishMovementAllowance(first->getMovementRange(0)));
	EXPECT_GT(first->pursuitMovementRemaining, 0);
}

TEST_F(NewHorizonsVanishTest, UnselectedFlankingKillDoesNotGrantMovement)
{
	prepare(false);
	addFlanker();
	activate(first);
	ASSERT_TRUE(battle()->battleIsShroudFlankingAttack(flank()));
	ASSERT_TRUE(hit(first, target, first->getPosition()));
	EXPECT_FALSE(target->alive());
	EXPECT_EQ(first->pursuitMovementRemaining, 0);
}

TEST_F(NewHorizonsVanishTest, OrdinaryFrontKillDoesNotTriggerSelectedVanish)
{
	prepare();
	for(const auto hex : target->getSurroundingHexes())
	{
		if(!hex.isAvailable() || battle()->battleGetStackByPos(hex))
			continue;
		auto * candidate = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), hex, 100);
		ASSERT_NE(candidate, nullptr);
		if(!battle()->battleIsShroudFlankingAttack(BattleAttackInfo(candidate, target, 0, false)))
		{
			first = candidate;
			break;
		}
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		remove.changedStacks.emplace_back(candidate->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}
	ASSERT_NE(first, nullptr);
	activate(first);
	ASSERT_FALSE(battle()->battleIsShroudFlankingAttack(flank()));
	ASSERT_TRUE(hit(first, target, first->getPosition()));
	EXPECT_FALSE(target->alive());
	EXPECT_EQ(first->pursuitMovementRemaining, 0);
}

TEST_F(NewHorizonsVanishTest, OffTurnFlankingRetaliationKillDoesNotInterruptActor)
{
	prepare();
	addFlanker();
	ASSERT_TRUE(battle()->battleIsShroudFlankingAttack(flank()));
	activate(target);
	ASSERT_TRUE(hit(target, first, target->getPosition()));
	EXPECT_FALSE(target->alive());
	EXPECT_TRUE(first->alive());
	EXPECT_EQ(first->pursuitMovementRemaining, 0);
}

TEST_F(NewHorizonsVanishTest, PursuitCompositionUsesMaximumAndKeepsPursuitCalculation)
{
	prepare();
	select(attackerSideHero, "new-horizons:offense", "new-horizons:offense.pursuit", MasteryLevel::BASIC);
	addFlanker();
	activate(first);
	const int before = static_cast<int>(first->getMovementRange(0));
	ASSERT_TRUE(hit(first, target, first->getPosition()));
	ASSERT_FALSE(target->alive());
	EXPECT_EQ(first->pursuitMovementRemaining,
		std::max(before, newHorizonsShroud::vanishMovementAllowance(first->getMovementRange(0))));
	EXPECT_EQ(first->pursuitMovementRemaining, before);
	EXPECT_LT(first->pursuitMovementRemaining, before + newHorizonsShroud::vanishMovementAllowance(first->getMovementRange(0)));
}

TEST_F(NewHorizonsVanishTest, DetachedActualKillProjectsBudgetAndRejectsReactionGrant)
{
	prepare();
	addFlanker();
	activate(first);
	VanishEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::ATTACKER);
	auto candidate = flank();
	const auto preview = AttackPossibility::evaluate(candidate, first->getPosition(), cache, parent);
	ASSERT_TRUE(preview.defenderDead);
	ASSERT_NE(preview.effectPreview, nullptr);
	ASSERT_NE(preview.attackerState, nullptr);
	EXPECT_EQ(preview.attackerState->pursuitMovementRemaining,
		newHorizonsShroud::vanishMovementAllowance(preview.attackerState->getMovementRange(0)));
	EXPECT_EQ(parent->getForUpdate(first->unitId())->pursuitMovementRemaining, 0);
	EXPECT_EQ(first->pursuitMovementRemaining, 0);
	candidate.retaliation = true;
	const auto reaction = AttackPossibility::evaluate(candidate, first->getPosition(), cache, parent);
	ASSERT_NE(reaction.attackerState, nullptr);
	EXPECT_EQ(reaction.attackerState->pursuitMovementRemaining, 0);
	candidate = flank();
	candidate.secondaryAttack = true;
	const auto collateral = AttackPossibility::evaluate(candidate, first->getPosition(), cache, parent);
	ASSERT_NE(collateral.attackerState, nullptr);
	EXPECT_EQ(collateral.attackerState->pursuitMovementRemaining, 0);
}

TEST_F(NewHorizonsVanishTest, ActualCollateralKillWithSurvivingPrimaryDoesNotTrigger)
{
	prepare();
	const auto targetPosition = target->getPosition();
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	remove.changedStacks.emplace_back(target->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), targetPosition, 1000);
	first = addStack(BattleSide::ATTACKER, creatureByName("core:hydra"), BattleHex(40), 1);
	ASSERT_NE(first, nullptr);
	bool positioned = false;
	// A double-wide rear contact can have its head two hexes from the target.
	// Search legal full footprints, not only single-hex head adjacency.
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		bool legal = hex.isAvailable();
		for(const auto footprint : first->getHexes(hex))
			if(!footprint.isAvailable() || (battle()->battleGetStackByPos(footprint)
				&& battle()->battleGetStackByPos(footprint) != first))
				legal = false;
		if(!legal || !battle()->isMeleeAttackPossible(first, target, hex, targetPosition))
			continue;
		auto attack = flank();
		attack.attackerPos = hex;
		if(!battle()->battleIsShroudFlankingAttack(attack))
			continue;
		first->setPosition(hex);
		positioned = true;
		break;
	}
	ASSERT_TRUE(positioned);
	CStack * collateral = nullptr;
	for(const auto hex : first->getSurroundingHexes())
		if(hex.isAvailable() && !battle()->battleGetStackByPos(hex))
		{
			collateral = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), hex, 1);
			break;
		}
	ASSERT_NE(collateral, nullptr);
	forceMaximumDamage(first);
	activate(first);
	ASSERT_TRUE(battle()->battleIsShroudFlankingAttack(flank()));
	ASSERT_TRUE(hit(first, target, first->getPosition()));
	EXPECT_TRUE(target->alive());
	EXPECT_FALSE(collateral->alive());
	EXPECT_EQ(first->pursuitMovementRemaining, 0);
}
