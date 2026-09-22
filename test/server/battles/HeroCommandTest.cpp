/*
 * HeroCommandTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/SideInBattle.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/bonuses/Bonus.h"
// Full game-state roundtrips instantiate serializers for the complete object graph.
#include "../../../lib/CPlayerState.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/campaign/CampaignState.h"
#include "../../../lib/gameState/CGameStateCampaign.h"
#include "../../../lib/gameState/TavernHeroesPool.h"
#include "../../../lib/mapObjects/MiscObjects.h"
#include "../../../lib/mapObjects/ObjectTemplate.h"
#include "../../../lib/mapObjects/Quest.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/TownBuildingInstance.h"
#include "../../../lib/mapping/CCastleEvent.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/rmg/CMapGenOptions.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/serializer/CMemorySerializer.h"

class HeroCommandTest : public HeroCommandFixture {};

class ShockAssaultTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepareShockAssault(bool selectPerk)
	{
		prepareCommands();
		const auto decoded = SecondarySkill::decode("new-horizons:offense");
		ASSERT_GE(decoded, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(selectPerk)
			attackerSideHero->applyPerkSelection({
				"new-horizons:offense", "new-horizons:offense.shockAssault"});
		ASSERT_EQ(attackerSideHero->hasActivePerk(
			"new-horizons:offense", "new-horizons:offense.shockAssault"), selectPerk);
	}
};

TEST_F(HeroCommandTest, HeroOrderStatePacketRoundTripsThroughClientPackPointer)
{
	BattleHeroOrderStateChanged outgoing;
	outgoing.battleID = BattleID(7);
	outgoing.side = BattleSide::ATTACKER;
	HeroOrderState state;
	state.command = HeroCommand::PROTECT;
	state.issuedRound = 3;
	state.primaryTargetUnitId = 11;
	state.secondaryTargetUnitId = 12;
	state.protectIntercepted = true;
	outgoing.state = state;

	const CPackForClient & base = outgoing;
	auto polymorphic = CMemorySerializer::deepCopy(base);
	const auto * registered = dynamic_cast<const BattleHeroOrderStateChanged *>(polymorphic.get());
	ASSERT_NE(registered, nullptr);
	EXPECT_EQ(registered->battleID, outgoing.battleID);
	EXPECT_EQ(registered->side, outgoing.side);
	ASSERT_TRUE(registered->state);
	EXPECT_EQ(*registered->state, *outgoing.state);
}

TEST_F(HeroCommandTest, CreatureLocationSpellPacketPreservesUnitZeroAndLanding)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	const auto * source = battle()->battleGetUnitByID(0);
	ASSERT_NE(source, nullptr);
	ASSERT_EQ(source->unitId(), 0u);
	const BattleHex landing(71);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::TELEPORT;
	action.aimToHex(BattleHex(88)); // setTarget must discard this stale destination.
	action.setTarget(battle::Target{battle::Destination(source), battle::Destination(landing)});

	CMemorySerializer memory;
	memory.oser & action;
	BattleAction decoded;
	memory.iser & decoded;
	EXPECT_EQ(decoded.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(decoded.side, BattleSide::ATTACKER);
	EXPECT_EQ(decoded.spell, SpellID::TELEPORT);
	const auto target = decoded.getTarget(battle());
	ASSERT_EQ(target.size(), 2u);
	EXPECT_EQ(target[0].unitValue, source);
	EXPECT_EQ(target[0].hexValue, source->getPosition());
	EXPECT_EQ(target[1].unitValue, nullptr);
	EXPECT_EQ(target[1].hexValue, landing);
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
}

TEST_F(HeroCommandTest, LegacyGameHasNoCommands)
{
	useCommands = false;
	prepareCommands();
	EXPECT_FALSE(battle()->battleUsesHeroCommands());
	EXPECT_FALSE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(gameState()->getHeroCommandRules().isNull());
}

TEST_F(HeroCommandTest, ChargeChangesRealDamageWithoutManaOrCreatureTurn)
{
	prepareCommands();
	auto * from = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * to = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	const auto active = battle()->getActiveStackID();
	const auto mana = attackerSideHero->mana;
	const auto before = battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min;
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_GT(battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min, before);
	EXPECT_EQ(attackerSideHero->mana, mana);
	EXPECT_EQ(battle()->getActiveStackID(), active);
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::CHARGE);
	EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
	advanceRound();
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::NONE);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min, before);
}

TEST_F(HeroCommandTest, HoldTheLineReducesRealIncomingPhysicalDamage)
{
	prepareCommands();
	auto * ours = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	const auto before = battle()->calculateDmgRange(BattleAttackInfo(enemy, ours, 0, false)).damage.min;
	const auto shotBefore = battle()->calculateDmgRange(BattleAttackInfo(enemy, ours, 0, true)).damage.min;
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	ASSERT_EQ(server.battleLogLines.size(), 1);
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("Hold the Line!"));
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("hold position"));
	EXPECT_LT(battle()->calculateDmgRange(BattleAttackInfo(enemy, ours, 0, false)).damage.min, before);
	// Hold the Line covers all physical creature damage, including missiles.
	EXPECT_LT(battle()->calculateDmgRange(BattleAttackInfo(enemy, ours, 0, true)).damage.min, shotBefore);
	advanceRound();
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(enemy, ours, 0, false)).damage.min, before);
}

TEST_F(HeroCommandTest, ChargeExpiresAtTheRoundBoundary)
{
	prepareCommands();
	auto * from = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * to = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	const auto before = battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min;
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_GT(battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min, before);
	advanceRound();
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min, before);
}

TEST_F(ShockAssaultTest, ChargeWithShockAssaultIgnoresExactlyQuarterTargetDefense)
{
	prepareShockAssault(true);
	auto * from = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * to = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);

	const auto ordinary = battle()->calculateDmgRange(BattleAttackInfo(from, to, 0, false)).damage.min;
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	// Basic Offense already contributes 10%. Shock Assault leaves 15 Defense,
	// giving a further 25% attack/defense factor; Charge adds its ordinary 10%.
	EXPECT_EQ(ordinary, 5500);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min, 7250);
}

TEST_F(ShockAssaultTest, ShockAssaultNeedsTheChargeOrderAndThreeHexThreshold)
{
	prepareShockAssault(true);
	auto * from = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * to = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);

	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min, 5500);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(from, to, 2, false)).damage.min, 5500);
	// Charge is side-owned: issuing it for the attacker does not empower an
	// otherwise identical melee blow made by the defender.
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(to, from, 3, false)).damage.min, 5000);
}

TEST_F(ShockAssaultTest, ShockAssaultDoesNotChangeUnselectedOrRangedOrNonPhysicalDamage)
{
	prepareShockAssault(false);
	auto * from = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * to = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);

	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min, 6000);
}

TEST_F(ShockAssaultTest, ShockAssaultDoesNotApplyToRangedOrNonPhysicalDamage)
{
	prepareShockAssault(true);
	auto * from = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(72), 100);
	auto * to = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(73), 100);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	BattleAttackInfo ranged(from, to, 3, true);
	BattleAttackInfo rangedWithoutCharge(from, to, 0, true);
	EXPECT_EQ(battle()->calculateDmgRange(ranged).damage.min,
		battle()->calculateDmgRange(rangedWithoutCharge).damage.min);
	BattleAttackInfo nonPhysical(from, to, 3, false);
	nonPhysical.physicalDamage = false;
	// The Charge bonus itself is not a physical-only rule; only Shock Assault's
	// defense penetration is suppressed for this synthetic non-physical blow.
	EXPECT_EQ(battle()->calculateDmgRange(nonPhysical).damage.min, 6000);
}

TEST_F(ShockAssaultTest, ChargeAndShockAssaultApplyOnlyToTheFirstPrimaryBlow)
{
	prepareShockAssault(true);
	auto * from = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * to = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));

	BattleAttackInfo collateral(from, to, 3, false);
	collateral.secondaryAttack = true;
	EXPECT_EQ(battle()->calculateDmgRange(collateral).damage.min, 5500);

	auto * mutableBattle = const_cast<BattleInfo *>(dynamic_cast<const BattleInfo *>(battle()->getBattle()));
	ASSERT_NE(mutableBattle, nullptr);
	ASSERT_TRUE(mutableBattle->consumeHeroOrderUnit(BattleSide::ATTACKER, from->unitId()));
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min, 5500);
}

TEST_F(HeroCommandTest, RiposteBoostsOnlyRetaliationDamage)
{
	prepareCommands();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	BattleAttackInfo ordinary(attacker, defender, 0, false);
	const auto before = battle()->calculateDmgRange(ordinary).damage.min;
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	ASSERT_EQ(server.battleLogLines.size(), 1);
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("Riposte!"));
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("take less melee damage"));
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("retaliate more fiercely"));
	ordinary.retaliation = true;
	EXPECT_GT(battle()->calculateDmgRange(ordinary).damage.min, before);
}

TEST_F(HeroCommandTest, BracePreemptiveStrikeUsesItsOwnDamageFormula)
{
	prepareCommands();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	BattleAttackInfo incoming(attacker, defender, 0, false);
	const auto before = battle()->calculateDmgRange(incoming).damage.min;
	ASSERT_TRUE(issue(HeroCommand::BRACE));
	ASSERT_EQ(server.battleLogLines.size(), 1);
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("Brace!"));
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("moves at least 3 hexes"));
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("before a melee attack"));
	incoming.bracePreemptive = true;
	// Brace is a final multiplier: with the fixture's zero hero defense it is
	// exactly 50% of the ordinary blow, independent of additive Offense.
	EXPECT_EQ(battle()->calculateDmgRange(incoming).damage.min, before / 2);
	EXPECT_TRUE(battle()->battleCanTriggerHeroOrderBrace(defender, attacker, 3, false, false));
	EXPECT_TRUE(battle()->battleCanTriggerHeroOrderBrace(defender, attacker, 3, false, false));
	EXPECT_FALSE(battle()->battleCanTriggerHeroOrderBrace(defender, attacker, 2, false, false));
}

TEST_F(HeroCommandTest, ProtectRedirectsOneAdjacentWardAttack)
{
	prepareCommands();
	auto * protector = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * ward = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(71), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(72), 100);
	ASSERT_EQ(BattleHex::getDistance(protector->getPosition(), ward->getPosition()), 1);
	ASSERT_EQ(battle()->battleResolveHeroOrderTarget(enemy, ward, false), ward);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			protector->unitId(), ward->unitId())));
	ASSERT_EQ(server.battleLogLines.size(), 1);
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("Protect! Protector: Angels. Ward: Angels."));
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("first qualifying melee attack"));
	const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	EXPECT_EQ(state->primaryTargetUnitId, protector->unitId());
	EXPECT_EQ(state->secondaryTargetUnitId, ward->unitId());
	EXPECT_EQ(battle()->battleResolveHeroOrderTarget(enemy, ward, false), protector);
	ASSERT_TRUE(battle()->interceptHeroOrderProtect(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleResolveHeroOrderTarget(enemy, ward, false), ward);
}

TEST_F(HeroCommandTest, ProtectReductionIsScopedToTheInterceptedBlowAndStateIsReplicated)
{
	prepareCommands();
	auto * protector = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * ward = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(71), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(72), 100);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			protector->unitId(), ward->unitId())));
	const auto normal = battle()->calculateDmgRange(BattleAttackInfo(enemy, protector, 0, false)).damage.min;
	BattleAttackInfo intercepted(enemy, protector, 0, false);
	intercepted.protectIntercepted = true;
	const auto reduced = battle()->calculateDmgRange(intercepted).damage.min;
	EXPECT_LT(reduced, normal);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(enemy, protector, 0, false)).damage.min, normal);
	blockRetaliation(protector);
	blockRetaliation(ward);
	const auto statePacketsBeforeAttack = server.orderStateUpdates.size();
	ASSERT_TRUE(attack(enemy, ward->getPosition()));
	ASSERT_GT(server.orderStateUpdates.size(), statePacketsBeforeAttack);
	ASSERT_TRUE(server.orderStateUpdates.back().state);
	EXPECT_TRUE(server.orderStateUpdates.back().state->protectIntercepted);
}

TEST_F(HeroCommandTest, ProtectExpiresPermanentlyAfterFullFootprintSeparation)
{
	prepareCommands();
	auto * protector = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * ward = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(71), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(72), 100);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			protector->unitId(), ward->unitId())));
	BattleStackMoved separated;
	separated.battleID = BattleID(0);
	separated.stack = ward->unitId();
	separated.tilesToMove.insert(BattleHex(74));
	gameHandler->sendAndApply(separated);
	EXPECT_TRUE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER)->protectBroken);
	BattleStackMoved reunited = separated;
	reunited.tilesToMove.clear();
	reunited.tilesToMove.insert(BattleHex(71));
	gameHandler->sendAndApply(reunited);
	EXPECT_EQ(battle()->battleResolveHeroOrderTarget(enemy, ward, false), ward);
}

TEST_F(HeroCommandTest, FlankRaisesTheFirstDistinctSideAttack)
{
	prepareCommands();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * secondAttacker = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(54), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	BattleAttackInfo attack(attacker, defender, 0, false);
	const auto before = battle()->calculateDmgRange(attack).damage.min;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::FLANK, defender->unitId())));
	ASSERT_EQ(server.battleLogLines.size(), 1);
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("Flank! Target: Angels"));
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("exploit new sides this round"));
	const auto side = battle()->battleHeroOrderFlankSide(attacker, defender);
	ASSERT_NE(side, 0);
	const auto firstSide = battle()->calculateDmgRange(attack).damage.min;
	EXPECT_GT(firstSide, before);
	ASSERT_TRUE(battle()->recordHeroOrderFlankSide(BattleSide::ATTACKER, defender->unitId(), side));
	EXPECT_EQ(battle()->battleGetHeroOrderState(BattleSide::ATTACKER)->flankFor(defender->unitId())->sideMask, side);
	BattleAttackInfo secondAttack(secondAttacker, defender, 0, false);
	const auto secondSide = battle()->battleHeroOrderFlankSide(secondAttacker, defender);
	ASSERT_NE(secondSide, 0);
	ASSERT_NE(secondSide, side);
	EXPECT_GT(battle()->calculateDmgRange(secondAttack).damage.min, firstSide);
}

TEST_F(HeroCommandTest, SecondWindActivatesMovedStackWithDirectDamagePenalty)
{
	prepareCommands();
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	target->movedThisRound = true;
	BattleAttackInfo attack(target, enemy, 0, false);
	const auto before = battle()->calculateDmgRange(attack).damage.min;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::SECOND_WIND, target->unitId())));
	ASSERT_EQ(server.battleLogLines.size(), 1);
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("Second Wind! Target: Angels"));
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("One reduced-strength activation"));
	const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	EXPECT_TRUE(state->secondWindActive);
	EXPECT_EQ(battle()->getActiveStackID(), target->unitId());
	EXPECT_LT(battle()->calculateDmgRange(attack).damage.min, before);
}

TEST_F(HeroCommandTest, LegacyDoctrineIdsAreNeverIssuableOrExposed)
{
	prepareCommands();
	for(const auto command : {HeroCommand::AGGRESSIVE, HeroCommand::DEFENSIVE})
	{
		EXPECT_FALSE(heroCommands::supportedByRules(battle()->getHeroCommandRules(), command));
		EXPECT_FALSE(issue(command));
	}
	EXPECT_EQ(battle()->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::NONE);
	EXPECT_TRUE(battle()->battleActiveUnit()->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->empty());
	EXPECT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(battle()->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::NONE);
}

TEST_F(HeroCommandTest, WrongSideTargetsAndInvalidIdentifierAreRejectedBeforeState)
{
	prepareCommands();
	const auto starts = server.startedActions.size();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE)));
	EXPECT_FALSE(issue(static_cast<HeroCommand>(127)));
	auto malformed = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE);
	malformed.aimToUnit(battle()->battleActiveUnit());
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), malformed));
	EXPECT_EQ(server.startedActions.size(), starts);
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_TRUE(issue(HeroCommand::CHARGE));
}

class HeroActionBudgetTest : public HeroCommandFixture, public ::testing::WithParamInterface<std::tuple<int, int>> {};

TEST_P(HeroActionBudgetTest, EverySecondSpellOrderCombinationIsRejected)
{
	prepareCommands(true);
	const auto [first, second] = GetParam();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), heroAction(first)));
	const auto mana = attackerSideHero->mana;
	const auto starts = server.startedActions.size();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), heroAction(second)));
	EXPECT_EQ(attackerSideHero->mana, mana);
	EXPECT_EQ(server.startedActions.size(), starts);
	advanceRound();
	EXPECT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
}

INSTANTIATE_TEST_SUITE_P(AllNine, HeroActionBudgetTest,
	::testing::Combine(::testing::Values(0, 1, 2), ::testing::Values(0, 1, 2)));

TEST_F(HeroCommandTest, BattleSideAndActionRoundTripAndOldSideDefaults)
{
	prepareCommands();
	CMemorySerializer memory;
	SideInBattle source(gameState().get());
	source = battle()->getSide(BattleSide::ATTACKER);
	source.heroCommandUsed = true;
	source.activeDoctrine = HeroCommand::AGGRESSIVE;
	auto action = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::DEFENSIVE);
	memory.oser & source;
	memory.oser & action;
	SideInBattle restored(gameState().get());
	BattleAction decoded;
	memory.iser & restored;
	memory.iser & decoded;
	EXPECT_TRUE(restored.heroCommandUsed);
	EXPECT_EQ(restored.activeDoctrine, HeroCommand::NONE);
	EXPECT_EQ(decoded.command, HeroCommand::DEFENSIVE);
	EXPECT_EQ(decoded.actionType, EActionType::HERO_COMMAND);

	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::TOWN_CUSTOM_INITIAL_GARRISON;
	legacy.iser.version = ESerializationVersion::TOWN_CUSTOM_INITIAL_GARRISON;
	SideInBattle old(gameState().get());
	legacy.oser & old;
	legacy.iser & restored;
	EXPECT_FALSE(restored.heroCommandUsed);
	EXPECT_EQ(restored.activeDoctrine, HeroCommand::NONE);
	EXPECT_EQ(restored.activeOrder, HeroCommand::NONE);
}

TEST_F(HeroCommandTest, LegacyAdvanceIdentitySurvivesSideDecodeUntilBattleNormalization)
{
	prepareCommands();
	SideInBattle source(gameState().get());
	source = battle()->getSide(BattleSide::ATTACKER);
	source.heroCommandUsed = true;
	source.activeOrder = HeroCommand::ADVANCE;

	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::HERO_COMMANDS;
	memory.iser.version = ESerializationVersion::HERO_COMMANDS;
	memory.oser & source;
	SideInBattle restored(gameState().get());
	memory.iser & restored;

	EXPECT_TRUE(restored.heroCommandUsed);
	EXPECT_EQ(restored.activeDoctrine, HeroCommand::NONE);
	EXPECT_EQ(restored.activeOrder, HeroCommand::ADVANCE);
}

TEST_F(HeroCommandTest, PerGameRulesSnapshotRoundTripsAndRefuseLossyLegacyWrites)
{
	startGame();
	ASSERT_EQ(gameState()->getHeroCommandRules()["rulesetVersion"].Integer(), heroCommands::ORDERS_ONLY_RULESET_VERSION);
	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & *gameState());
	CGameState restored;
	current.iser.cb = &restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.getHeroCommandRules(), gameState()->getHeroCommandRules());

	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::TOWN_CUSTOM_INITIAL_GARRISON;
	EXPECT_THROW(legacy.oser & *gameState(), std::runtime_error);
}

TEST(HeroCommandRulesTest, NamedSettingsArrayLoadsRealContent)
{
	GameSettings settings;
	JsonNode files;
	files.Vector().emplace_back("config/newHorizonsCombat");
	files.setModScope(ModScope::scopeBuiltin());
	settings.loadBase(files);
	const auto & rules = settings.getValue(EGameSettings::COMBAT_HERO_COMMANDS);
	EXPECT_EQ(rules["rulesetVersion"].Integer(), heroCommands::ORDERS_ONLY_RULESET_VERSION);
	EXPECT_NO_THROW(heroCommands::validateRules(rules));
}

TEST(HeroCommandRulesTest, FormulaIsCoefficientBasedAndUnknownRulesFailClosed)
{
	const JsonNode file(JsonPath::builtin("config/newHorizonsCombat"));
	auto rules = file["combat"]["heroCommands"];
	EXPECT_EQ(heroCommands::coefficient(rules["commands"]["charge"]["effects"]["meleeDamagePercent"], 20, 0), 14);
	rules["rulesetVersion"].Integer() = 2;
	EXPECT_THROW(heroCommands::validateRules(rules), std::runtime_error);
}
