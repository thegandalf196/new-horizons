/*
 * HypotheticWallTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/Problem.h"

namespace
{
class WallEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit WallEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class HypotheticWallTest : public HeroCommandFixture
{
protected:
	std::shared_ptr<WallEnvironment> environment;
	std::shared_ptr<CPlayerBattleCallback> callback;

	void prepareSiege()
	{
		ASSERT_NO_FATAL_FAILURE(startGame(true));
		const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
		ASSERT_EQ(towns.size(), 1u);
		ASSERT_GT(towns.front()->fortificationsLevel().wallsHealth, 0);
		ASSERT_NO_FATAL_FAILURE(startBattle(towns.front()));
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		environment = std::make_shared<WallEnvironment>(gameState());
		callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	}
};

TEST_F(HypotheticWallTest, FieldBattleSnapshotsDoNotInventWallsGatesOrBlockedTiles)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	environment = std::make_shared<WallEnvironment>(gameState());
	callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	HypotheticBattle child(environment.get(), parent);
	for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
	{
		const auto part = static_cast<EWallPart>(index);
		EXPECT_EQ(parent->battleGetWallState(part), EWallState::NONE);
		EXPECT_EQ(child.battleGetWallState(part), EWallState::NONE);
	}
	EXPECT_EQ(parent->battleGetGateState(), EGateState::NONE);
	EXPECT_EQ(child.battleGetGateState(), EGateState::NONE);
	EXPECT_FALSE(parent->hasWallChanges());
	EXPECT_FALSE(child.hasWallChanges());
	const auto liveAccess = battle()->getAccessibility();
	const auto childAccess = child.getAccessibility();
	for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
		EXPECT_EQ(childAccess[hex], liveAccess[hex]);
}

TEST_F(HypotheticWallTest, PendingDemonicGateReservationsSurviveNestedBattleProxies)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	const auto imp = CreatureID(CreatureID::decode("core:imp"));
	ASSERT_TRUE(imp.hasValue());
	const BattleHex landing(8, 5);
	battle()->getSide(BattleSide::ATTACKER).pendingDemonicGates.push_back(
		{imp, 12, landing, battle()->getRound() + 1});

	environment = std::make_shared<WallEnvironment>(gameState());
	callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	HypotheticBattle child(environment.get(), parent);
	for(const auto * projection : {parent.get(), &child})
	{
		const auto accessibility = projection->getAccessibility();
		EXPECT_TRUE(accessibility.isDemonicGateReserved(landing));
		EXPECT_EQ(accessibility[landing.toInt()], EAccessibility::DEMONIC_GATE_RESERVED);
		EXPECT_FALSE(accessibility.accessible(landing, false, BattleSide::DEFENDER));
	}
}

TEST_F(HypotheticWallTest, ChangedWallInvalidatesOnlyLocalProjectionAndNestedCopies)
{
	ASSERT_NO_FATAL_FAILURE(prepareSiege());
	const auto original = battle()->battleGetWallState(EWallPart::BOTTOM_WALL);
	ASSERT_GT(static_cast<int>(original), static_cast<int>(EWallState::DESTROYED));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	HypotheticBattle child(environment.get(), parent);
	EXPECT_FALSE(child.hasWallChanges());
	child.setWallState(EWallPart::BOTTOM_WALL, original);
	EXPECT_FALSE(child.hasWallChanges());
	child.setWallState(EWallPart::BOTTOM_WALL, EWallState::DESTROYED);
	EXPECT_TRUE(child.hasWallChanges());
	EXPECT_EQ(child.battleGetWallState(EWallPart::BOTTOM_WALL), EWallState::DESTROYED);
	EXPECT_EQ(parent->battleGetWallState(EWallPart::BOTTOM_WALL), original);
	EXPECT_FALSE(parent->hasWallChanges());
	EXPECT_EQ(battle()->battleGetWallState(EWallPart::BOTTOM_WALL), original);
}

TEST_F(HypotheticWallTest, CanonicalStructuralHPIsForwardedAndCopiedIntoNestedProjection)
{
	ASSERT_NO_FATAL_FAILURE(prepareSiege());

	// This fixture normally exercises legacy fortifications. Install the
	// canonical New Horizons snapshot explicitly so this test remains focused
	// on callback/projection plumbing rather than capability fixture setup.
	battle()->si.canonicalStructuralHP = true;
	battle()->si.structuralHP[EWallPart::BOTTOM_WALL] = 300;
	battle()->si.structuralHP[EWallPart::GATE] = 450;
	battle()->si.structuralHP[EWallPart::BOTTOM_TOWER] = 350;
	battle()->si.wallState[EWallPart::BOTTOM_WALL] = EWallState::INTACT;
	battle()->si.wallState[EWallPart::GATE] = EWallState::INTACT;
	battle()->si.wallState[EWallPart::BOTTOM_TOWER] = EWallState::INTACT;

	EXPECT_EQ(callback->getWallStructuralHP(EWallPart::BOTTOM_WALL), 300);
	EXPECT_EQ(callback->getWallStructuralHP(EWallPart::GATE), 450);
	EXPECT_EQ(callback->getWallStructuralHP(EWallPart::BOTTOM_TOWER), 350);

	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	EXPECT_EQ(parent->getWallStructuralHP(EWallPart::BOTTOM_WALL), 300);
	EXPECT_EQ(parent->getWallStructuralHP(EWallPart::GATE), 450);
	EXPECT_EQ(parent->getWallStructuralHP(EWallPart::BOTTOM_TOWER), 350);

	parent->setWallStructuralHP(EWallPart::BOTTOM_WALL, 140);
	EXPECT_EQ(parent->getWallStructuralHP(EWallPart::BOTTOM_WALL), 140);
	EXPECT_EQ(parent->getWallState(EWallPart::BOTTOM_WALL), EWallState::DAMAGED);

	HypotheticBattle child(environment.get(), parent);
	EXPECT_EQ(child.getWallStructuralHP(EWallPart::BOTTOM_WALL), 140);
	EXPECT_EQ(child.getWallState(EWallPart::BOTTOM_WALL), EWallState::DAMAGED);
	EXPECT_EQ(child.getWallStructuralHP(EWallPart::GATE), 450);
	EXPECT_EQ(child.getWallStructuralHP(EWallPart::BOTTOM_TOWER), 350);
	EXPECT_EQ(battle()->getWallStructuralHP(EWallPart::BOTTOM_WALL), 300);
}

TEST_F(HypotheticWallTest, CanonicalCatapultProjectionConsumesWallGateAndTowerHPSuccessively)
{
	ASSERT_NO_FATAL_FAILURE(prepareSiege());
	battle()->si.canonicalStructuralHP = true;
	for(const auto part : {EWallPart::BOTTOM_WALL, EWallPart::GATE, EWallPart::BOTTOM_TOWER})
	{
		battle()->si.structuralHP[part] = SiegeInfo::maximumStructuralHP(part);
		battle()->si.wallState[part] = EWallState::INTACT;
	}

	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	const auto apply = [&](EWallPart part, int damage)
	{
		CatapultAttack hit;
		hit.battleID = BattleID(0);
		hit.attackedPart = part;
		hit.damageDealt = 1; // legacy hit quality must not replace canonical HP
		hit.structuralDamage = damage;
		model->getServerCallback()->apply(hit);
	};

	apply(EWallPart::BOTTOM_WALL, 160);
	EXPECT_EQ(model->getWallStructuralHP(EWallPart::BOTTOM_WALL), 140);
	EXPECT_EQ(model->getWallState(EWallPart::BOTTOM_WALL), EWallState::DAMAGED);
	apply(EWallPart::BOTTOM_WALL, 160);
	EXPECT_EQ(model->getWallStructuralHP(EWallPart::BOTTOM_WALL), 0);
	EXPECT_EQ(model->getWallState(EWallPart::BOTTOM_WALL), EWallState::DESTROYED);

	apply(EWallPart::GATE, 225);
	EXPECT_EQ(model->getWallStructuralHP(EWallPart::GATE), 225);
	EXPECT_EQ(model->getWallState(EWallPart::GATE), EWallState::DAMAGED);
	apply(EWallPart::GATE, 230);
	EXPECT_EQ(model->getWallStructuralHP(EWallPart::GATE), 0);
	EXPECT_EQ(model->getWallState(EWallPart::GATE), EWallState::DESTROYED);

	apply(EWallPart::BOTTOM_TOWER, 175);
	EXPECT_EQ(model->getWallStructuralHP(EWallPart::BOTTOM_TOWER), 175);
	EXPECT_EQ(model->getWallState(EWallPart::BOTTOM_TOWER), EWallState::DAMAGED);
	apply(EWallPart::BOTTOM_TOWER, 175);
	EXPECT_EQ(model->getWallStructuralHP(EWallPart::BOTTOM_TOWER), 0);
	EXPECT_EQ(model->getWallState(EWallPart::BOTTOM_TOWER), EWallState::DESTROYED);
	EXPECT_EQ(battle()->getWallStructuralHP(EWallPart::BOTTOM_WALL), 300);
}

TEST_F(HypotheticWallTest, DestroyedGateBecomesPassableWithoutChangingParentOrLiveGate)
{
	ASSERT_NO_FATAL_FAILURE(prepareSiege());
	battle()->si.gateState = EGateState::CLOSED;
	battle()->setWallState(EWallPart::GATE, EWallState::DAMAGED);
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	HypotheticBattle child(environment.get(), parent);
	EXPECT_EQ(child.battleGetGateState(), EGateState::CLOSED);
	EXPECT_FALSE(child.getAccessibility().accessible(BattleHex::GATE_OUTER, false, BattleSide::ATTACKER));
	child.setWallState(EWallPart::GATE, EWallState::DESTROYED);
	EXPECT_EQ(child.battleGetGateState(), EGateState::DESTROYED);
	EXPECT_TRUE(child.getAccessibility().accessible(BattleHex::GATE_OUTER, false, BattleSide::ATTACKER));
	EXPECT_EQ(parent->battleGetGateState(), EGateState::CLOSED);
	EXPECT_EQ(battle()->battleGetGateState(), EGateState::CLOSED);
	EXPECT_EQ(battle()->battleGetWallState(EWallPart::GATE), EWallState::DAMAGED);
}

TEST_F(HypotheticWallTest, RealEarthquakeCanDestroyTheLastGateInModelAndAuthoritativeBattle)
{
	ASSERT_NO_FATAL_FAILURE(prepareSiege());
	// Siege defenders can act first. Reach an actual attacker spell window
	// through legal actions instead of submitting a request on the enemy turn.
	for(int actions = 0; actions < 100; ++actions)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		const auto owner = battle()->battleGetOwner(active);
		if(owner == PlayerColor(0))
			break;
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
			BattleID(0), owner, BattleAction::makeDefend(active)));
	}
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::EARTHQUAKE);
	attackerSideHero->mana = 1000;
	for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
	{
		const auto part = static_cast<EWallPart>(index);
		if(part != EWallPart::GATE)
			battle()->setWallState(part, EWallState::DESTROYED);
	}
	if(battle()->si.canonicalStructuralHP)
	{
		// Setting only the visual state cannot revive canonical HP after destruction.
		battle()->setWallStructuralHP(EWallPart::GATE, 1);
		ASSERT_EQ(battle()->getWallStructuralHP(EWallPart::GATE), 1);
	}
	else
	{
		battle()->setWallState(EWallPart::GATE, EWallState::DAMAGED);
	}
	battle()->si.gateState = EGateState::CLOSED;
	const auto * spell = SpellID(SpellID::EARTHQUAKE).toSpell();
	spells::BattleCast live(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&live);
	ASSERT_TRUE(battle()->hasFortifications());
	ASSERT_TRUE(!mechanics->isSmart() || mechanics->getCasterSide() == BattleSide::ATTACKER);
	ASSERT_TRUE(battle()->isWallPartAttackable(EWallPart::GATE));
	spells::detail::ProblemImpl castProblem;
	const bool canCast = spell->canBeCast(castProblem, battle(), spells::Mode::HERO, attackerSideHero);
	std::vector<std::string> castProblems;
	castProblem.getAll(castProblems);
	ASSERT_TRUE(canCast) << testing::PrintToString(castProblems);
	ASSERT_TRUE(mechanics->canBeCastAt({}));
	HypotheticBattle model(environment.get(), callback);
	spells::BattleCast projected(&model, attackerSideHero, spells::Mode::HERO, spell);
	projected.castEval(model.getServerCallback(), {});
	EXPECT_EQ(model.battleGetWallState(EWallPart::GATE), EWallState::DESTROYED);
	EXPECT_EQ(model.battleGetGateState(), EGateState::DESTROYED);
	EXPECT_TRUE(model.hasWallChanges());
	EXPECT_TRUE(model.getAccessibility().accessible(BattleHex::GATE_OUTER, false, BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleGetWallState(EWallPart::GATE), EWallState::DAMAGED);
	EXPECT_EQ(battle()->battleGetGateState(), EGateState::CLOSED);
	EXPECT_EQ(attackerSideHero->mana, 1000);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::EARTHQUAKE;
	action.aimToHex(BattleHex::INVALID);
	const auto cost = attackerSideHero->getSpellCost(spell);
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(battle()->battleGetOwner(active), PlayerColor(0));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(battle()->battleGetWallState(EWallPart::GATE), EWallState::DESTROYED);
	EXPECT_EQ(battle()->battleGetGateState(), EGateState::DESTROYED);
	EXPECT_EQ(attackerSideHero->mana, 1000 - cost);
}
