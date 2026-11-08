/*
 * NewHorizonsCrisisCommandTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "BattleStartSnapshotFixture.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/ScopeGuard.h"
#include "../../../lib/battle/NewHorizonsCrisisCommand.h"
#include "../../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#if ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../AI/BattleAI/BattleEvaluator.h"
#include "../../../AI/BattleAI/BattleAI.h"
#include "../../../AI/BattleAI/HeroDecisionPressure.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/StartInfo.h"
#include "../../../lib/callback/CBattleCallback.h"
#include "../../../AI/BattleAI/HypotheticBattle.h"
#endif

namespace
{
struct CrisisRandomStateArchive
{
	bool saving = true;
	std::string state;
	void operator&(std::string & value) { state = value; }
};
std::string crisisRandomState(CRandomGenerator & generator)
{
	CrisisRandomStateArchive archive;
	generator.serialize(archive);
	return archive.state;
}

// Corrupt only transmitted descriptor data, after the source's write preflight.
// This exercises real current-format decode admission without mutating live units.
class NewHorizonsCrisisCommandTest : public HeroCommandFixture
{
protected:
	CStack * actor = nullptr;
	CStack * victim = nullptr;
	CStack * reserve = nullptr;
	CStack * other = nullptr;
	BattleSide recipient = BattleSide::DEFENDER;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		useRevisedOrders = true;
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		ASSERT_FALSE(newHorizonsCrisisCommand::activeProfile(perks)) << "Current rules must not enable reactive Orders";
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}
	void select(CGHeroInstance * hero)
	{
		hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:orrin")));
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsCrisisCommand::SKILL)),
			MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({newHorizonsCrisisCommand::SKILL, "new-horizons:command.aggressiveCommander"});
		hero->applyPerkSelection({newHorizonsCrisisCommand::SKILL, newHorizonsCrisisCommand::PERK});
		ASSERT_TRUE(hero->hasActivePerk(newHorizonsCrisisCommand::SKILL, newHorizonsCrisisCommand::PERK));
		ASSERT_FALSE(newHorizonsCrisisCommand::eligible(hero));
	}
	void activate(const CStack * unit)
	{
		BattleSetActiveStack pack;
		pack.battleID = BattleID(0); pack.stack = unit->unitId(); pack.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(pack);
	}
	bool action(const CStack * unit, bool wait = false)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetActionController(unit),
			wait ? BattleAction::makeWait(unit) : BattleAction::makeDefend(unit));
	}
	void prepare(bool actorDies = false, bool both = false, bool selected = true)
	{
		startGame();
		recipient = actorDies ? BattleSide::ATTACKER : BattleSide::DEFENDER;
		if(selected) select(actorDies ? attackerSideHero : defenderSideHero);
		if(both) select(actorDies ? defenderSideHero : attackerSideHero);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::ATTACKER, creatureByName(actorDies ? "core:peasant" : "core:archangel"), BattleHex(leftHex), actorDies ? 1 : 100);
		victim = addStack(BattleSide::DEFENDER, creatureByName(actorDies ? "core:archangel" : "core:peasant"), BattleHex(rightHex), actorDies ? 100 : 1);
		reserve = addStack(recipient, creatureByName("core:archer"), BattleHex(actorDies ? 3 : 14, 3), 1000);
		other = addStack(actorDies ? BattleSide::DEFENDER : BattleSide::ATTACKER,
			creatureByName("core:pikeman"), BattleHex(actorDies ? 14 : 3, 7), 1000);
		if(!actorDies) blockRetaliation(victim);
		SetStackEffect quiet;
		quiet.battleID = BattleID(0);
		for(const auto * unit : {actor, victim, reserve, other})
			quiet.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{Bonus(BonusDuration::ONE_BATTLE,
				BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID())});
		gameHandler->sendAndApply(quiet);
		beginCombat();
		activate(actor);
	}
	bool kill()
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeMeleeAttack(actor, victim, actor->getPosition()));
	}
	bool issue(HeroCommand command = HeroCommand::HOLD_THE_LINE)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(recipient),
			BattleAction::makeHeroCommand(recipient, command));
	}
	void injure(const std::vector<CStack *> & units, uint32_t source)
	{
		StacksInjured pack;
		pack.battleID = BattleID(0);
		for(auto * unit : units)
		{
			auto & hit = pack.stacks.emplace_back();
			hit.attackerID = source; hit.stackAttacked = unit->unitId(); hit.damageAmount = unit->getAvailableHealth();
			CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), unit->acquireState());
		}
		gameHandler->sendAndApply(pack);
	}
};

struct CrisisOldPrefix
{
	using Version = ESerializationVersion;
	bool saving = true;
	bool loadingGamestate = false;
	int fields = 0;
	bool hasFeature(Version version) const { return version < Version::NEW_HORIZONS_CRISIS_COMMAND; }
	template<typename T> CrisisOldPrefix & operator&(T &)
	{
		++fields;
		throw std::runtime_error("Unexpected old-format prefix");
	}
};
}

TEST_F(NewHorizonsCrisisCommandTest, EnemyKillCompletesWithoutReactiveOrderOrCommandPayment)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto resources = battle()->getCommandResources(recipient);
	const auto allowances = battle()->getHeroActionAllowances(recipient);
	const auto serial = battle()->getActivationSerial();
	ASSERT_TRUE(kill());
	EXPECT_FALSE(victim->alive());
	EXPECT_TRUE(actor->movedThisRound);
	EXPECT_TRUE(std::ranges::none_of(server.stackActivations,
		[](const auto & event) { return event.reason == BattleUnitTurnReason::CRISIS_ORDER; }));
	EXPECT_FALSE(battle()->getCrisisCommandState().meaningful());
	EXPECT_EQ(battle()->getCommandResources(recipient), resources);
	EXPECT_EQ(battle()->getHeroActionAllowances(recipient), allowances);
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	EXPECT_TRUE(battle()->battleActiveUnit()->alive());
	EXPECT_GT(battle()->getActivationSerial(), serial);
}

TEST_F(NewHorizonsCrisisCommandTest, RetaliationKillCannotSuspendOriginalActionForAnOrder)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto resources = battle()->getCommandResources(recipient);
	const auto allowances = battle()->getHeroActionAllowances(recipient);
	ASSERT_TRUE(kill());
	EXPECT_FALSE(actor->alive());
	EXPECT_FALSE(battle()->getCrisisCommandState().meaningful());
	EXPECT_EQ(battle()->getCommandResources(recipient), resources);
	EXPECT_EQ(battle()->getHeroActionAllowances(recipient), allowances);
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	EXPECT_NE(battle()->battleActiveUnit(), actor);
}

TEST_F(NewHorizonsCrisisCommandTest, WholeInjuryBatchIncludingCloneAndSummonCreatesNoReactiveChoice)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	auto clone = victim->acquireState(); clone->cloned = true;
	BattleUnitsChanged update;
	update.battleID = BattleID(0);
	update.changedStacks.emplace_back(victim->unitId(), UnitChanges::EOperation::UPDATE);
	update.changedStacks.back().data = clone->save();
	gameHandler->sendAndApply(update);
	battle::UnitInfo info;
	info.id = battle()->battleNextUnitId(); info.type = creatureByName("core:peasant");
	info.side = BattleSide::DEFENDER; info.position = BattleHex(10, 5); info.count = 1; info.summoned = true;
	BattleUnitsChanged add; add.battleID = BattleID(0);
	add.changedStacks.emplace_back(info.id, UnitChanges::EOperation::ADD);
	info.save(add.changedStacks.back().data);
	gameHandler->sendAndApply(add);
	auto * summon = battle()->getStack(info.id, false);
	ASSERT_NE(summon, nullptr); ASSERT_TRUE(summon->isSummoned());
	const auto defenderResources = battle()->getCommandResources(BattleSide::DEFENDER);
	const auto attackerResources = battle()->getCommandResources(BattleSide::ATTACKER);
	const auto additions = server.unitAdditionRounds.size();
	injure({victim, summon}, actor->unitId());
	EXPECT_FALSE(victim->alive()); EXPECT_FALSE(summon->alive());
	EXPECT_FALSE(battle()->getCrisisCommandState().meaningful());
	EXPECT_EQ(battle()->getCommandResources(BattleSide::DEFENDER), defenderResources);
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER), attackerResources);
	EXPECT_EQ(server.unitAdditionRounds.size(), additions);
}

TEST_F(NewHorizonsCrisisCommandTest, ControlledDeathDoesNotTransferAReactiveOrderToEitherHero)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	SetStackEffect control;
	control.battleID = BattleID(0);
	control.toAdd.emplace_back(victim->unitId(), std::vector<Bonus>{Bonus(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID())});
	gameHandler->sendAndApply(control);
	ASSERT_EQ(battle()->battleGetOwner(victim), PlayerColor(0));
	const auto attackerActions = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto defenderActions = battle()->getHeroActionAllowances(BattleSide::DEFENDER);
	injure({victim}, actor->unitId());
	EXPECT_FALSE(battle()->getCrisisCommandState().meaningful());
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), attackerActions);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER), defenderActions);
}

TEST_F(NewHorizonsCrisisCommandTest, PuppetControlledDeathCreatesNoReactiveGrantAndKeepsPhysicalAllegiance)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	SetStackEffect control;
	control.battleID = BattleID(0);
	control.toAdd.emplace_back(victim->unitId(), std::vector<Bonus>{newHorizonsPuppetMaster::controlMarker(
		SpellID(SpellID::decode(std::string(newHorizonsPuppetMaster::SPELL_ID))), PlayerColor(0))});
	gameHandler->sendAndApply(control);
	ASSERT_EQ(battle()->battleGetOwner(victim), PlayerColor(1));
	ASSERT_EQ(battle()->battleGetActionController(victim), PlayerColor(0));
	const auto attackerActions = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto defenderActions = battle()->getHeroActionAllowances(BattleSide::DEFENDER);
	injure({victim}, actor->unitId());
	EXPECT_EQ(victim->unitSide(), BattleSide::DEFENDER);
	EXPECT_FALSE(battle()->getCrisisCommandState().meaningful());
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), attackerActions);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER), defenderActions);
}

TEST_F(NewHorizonsCrisisCommandTest, BattleFinalizationDoesNotExposeAChoice)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BattleUnitsChanged remove; remove.battleID = BattleID(0);
	remove.changedStacks.emplace_back(reserve->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	ASSERT_TRUE(kill());
	EXPECT_FALSE(std::any_of(server.stackActivations.begin(), server.stackActivations.end(), [](const auto & packet)
		{ return packet.reason == BattleUnitTurnReason::CRISIS_ORDER; }));
}

TEST_F(NewHorizonsCrisisCommandTest, UnselectedReplacementPerkDoesNotGrantAFreeOrder)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false));
	const auto resources = battle()->getCommandResources(recipient);
	ASSERT_TRUE(kill());
	EXPECT_FALSE(battle()->getCrisisCommandState().meaningful());
	EXPECT_EQ(battle()->getCommandResources(recipient), resources);
}

TEST_F(NewHorizonsCrisisCommandTest, PostDeathDescriptorKeepsNoSuspensionOrRetiredAllowance)
{
	ASSERT_NO_FATAL_FAILURE(prepare()); ASSERT_TRUE(kill());
	const auto descriptor = battleStartFixture::snapshot(*battle(), gameState().get());
	ASSERT_NE(descriptor, nullptr);
	EXPECT_FALSE(descriptor->getCrisisCommandState().meaningful());
	EXPECT_EQ(descriptor->getCommandResources(recipient), battle()->getCommandResources(recipient));
	EXPECT_EQ(descriptor->getHeroActionAllowances(recipient), battle()->getHeroActionAllowances(recipient));
}

TEST_F(NewHorizonsCrisisCommandTest, RetiredStatePacketRejectsWithoutChangingActionsResourcesOrActor)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto actions = battle()->getHeroActionAllowances(recipient);
	const auto resources = battle()->getCommandResources(recipient);
	const auto active = battle()->getActiveStackID();
	BattleCrisisCommandChanged forged;
	forged.battleID = BattleID(0); forged.state.used[0] = true;
	EXPECT_THROW(gameHandler->sendAndApply(forged), std::runtime_error);
	EXPECT_FALSE(battle()->getCrisisCommandState().meaningful());
	EXPECT_EQ(battle()->getHeroActionAllowances(recipient), actions);
	EXPECT_EQ(battle()->getCommandResources(recipient), resources);
	EXPECT_EQ(battle()->getActiveStackID(), active);
	CMemorySerializer bytes;
	EXPECT_THROW(bytes.oser & forged, std::runtime_error);
	EXPECT_TRUE(bytes.extractBuffer().empty());
}

TEST_F(NewHorizonsCrisisCommandTest, CurrentRulesRejectBeforeOlderSettingsHeroMapWorldAndLobbyPrefixes)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CrisisOldPrefix settings, hero, map, world, lobby;
	GameSettings captured;
	captured.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, defenderSideHero->getPerkState().rules);
	EXPECT_THROW(captured.serialize(settings), std::runtime_error); EXPECT_EQ(settings.fields, 0);
	EXPECT_THROW(defenderSideHero->serialize(hero), std::runtime_error); EXPECT_EQ(hero.fields, 0);
	EXPECT_THROW(gameState()->getMap().serialize(map), std::runtime_error); EXPECT_EQ(map.fields, 0);
	EXPECT_THROW(gameState()->serialize(world), std::runtime_error); EXPECT_EQ(world.fields, 0);
	LobbyStartGame start; start.initializedGameState = gameState();
	EXPECT_THROW(start.serialize(lobby), std::runtime_error); EXPECT_EQ(lobby.fields, 0);
}

TEST(NewHorizonsCrisisCommandProtocolTest, EmptyStateCodecRoundTripsButRetiredRawStateCannotBeAdmitted)
{
	newHorizonsCrisisCommand::State empty, decoded;
	CMemorySerializer valid;
	ASSERT_NO_THROW(valid.oser & empty);
	ASSERT_NO_THROW(valid.iser & decoded);
	EXPECT_FALSE(decoded.meaningful());
	newHorizonsCrisisCommand::State retired; retired.used[0] = true;
	CMemorySerializer rejected;
	EXPECT_THROW(rejected.oser & retired, std::runtime_error);
	EXPECT_TRUE(rejected.extractBuffer().empty());
	CMemorySerializer raw;
	raw.oser & retired.used & retired.pending & retired.returns; // Bypass writer only to exercise raw reader validation.
	EXPECT_THROW(raw.iser & decoded, std::runtime_error);
}

TEST(NewHorizonsCrisisCommandProtocolTest, CurrentAndOlderWritersRejectEveryRetiredOrderGrantBeforePrefixes)
{
	using Ledger = HeroActionAllowanceState;
	for(const auto source : {Ledger::GrantSource::CRISIS_COMMAND, Ledger::GrantSource::BATTLE_PLAN,
		Ledger::GrantSource::DOUBLE_COMMAND, Ledger::GrantSource::HEROIC_SPIRIT})
		for(const auto version : {ESerializationVersion::CURRENT, ESerializationVersion::NEW_HORIZONS_MULTIPLE_ORDERS})
		{
			Ledger::Grant grant;
			grant.id = 1; grant.allowance = Ledger::AllowanceKind::ORDER; grant.source = source;
			grant.grantedRound = 1; grant.expiryRound = 1;
			CMemorySerializer bytes; bytes.oser.version = version;
			EXPECT_THROW(bytes.oser & grant, std::runtime_error);
			EXPECT_TRUE(bytes.extractBuffer().empty());
		}
}
