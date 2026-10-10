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
#include "../../../lib/callback/CBattleCallback.h"
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
class CrisisDescriptorCorruptor
{
	battleStartFixture::DescriptorWriter writer;
	uint32_t anchor;
	bool missingReference;
public:
	using Version = ESerializationVersion;
	static constexpr bool saving = true;
	CrisisDescriptorCorruptor(BinarySerializer & encoder, uint32_t anchor, bool missingReference)
		: writer(encoder), anchor(anchor), missingReference(missingReference) {}
	bool hasFeature(Version version) const { return writer.hasFeature(version); }
	template<typename T> CrisisDescriptorCorruptor & operator&(T & value)
	{
		writer & value;
		return *this;
	}
	CrisisDescriptorCorruptor & operator&(newHorizonsCrisisCommand::State & value)
	{
		auto copy = value;
		if(missingReference) copy.returns.back().anchor = newHorizonsCrisisCommand::NO_UNIT - 1;
		writer & copy;
		return *this;
	}
	CrisisDescriptorCorruptor & operator&(std::vector<std::unique_ptr<CStack>> & units)
	{
		if(missingReference) { writer & units; return *this; }
		std::vector<std::unique_ptr<CStack>> descriptors;
		for(const auto & unit : units)
		{
			CStackBasicDescriptor base(unit->unitType()->getId(), unit->unitId() == anchor ? 0 : unit->unitBaseAmount());
			auto copy = std::make_unique<CStack>(&base, unit->unitOwner(), static_cast<int>(unit->unitId()),
				unit->unitSide(), unit->unitSlot());
			copy->initialPosition = unit->initialPosition;
			for(const auto & bonus : unit->getExportedBonusList()) copy->addNewBonus(std::make_shared<Bonus>(*bonus));
			descriptors.push_back(std::move(copy));
		}
		writer & descriptors;
		return *this;
	}
};

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
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		ASSERT_TRUE(newHorizonsCrisisCommand::activeProfile(perks)) << "Requires shipped active Crisis registration";
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}
	void select(CGHeroInstance * hero)
	{
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsCrisisCommand::SKILL)),
			MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({newHorizonsCrisisCommand::SKILL, "new-horizons:command.aggressiveCommander"});
		hero->applyPerkSelection({newHorizonsCrisisCommand::SKILL, newHorizonsCrisisCommand::PERK});
		ASSERT_TRUE(newHorizonsCrisisCommand::eligible(hero));
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
	void prepare(bool actorDies = false, bool both = false, bool selected = true, bool seize = false)
	{
		startGame();
		recipient = actorDies ? BattleSide::ATTACKER : BattleSide::DEFENDER;
		if(selected) select(actorDies ? attackerSideHero : defenderSideHero);
		if(both) select(actorDies ? defenderSideHero : attackerSideHero);
		if(seize)
		{
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:command")),
				MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:command", "new-horizons:command.aggressiveCommander"});
			attackerSideHero->applyPerkSelection({"new-horizons:command", "new-horizons:command.veteranCommander"});
			attackerSideHero->applyPerkSelection({"new-horizons:command", "new-horizons:command.seizeInitiative"});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:command", "new-horizons:command.seizeInitiative"));
		}
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
	bool decline()
	{
		const auto & state = battle()->getCrisisCommandState();
		const auto * anchor = battle()->getStack(state.returns.back().anchor);
		auto pass = BattleAction::makeNoAction(anchor); pass.side = recipient;
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(recipient), pass);
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
#if ENABLE_BATTLE_AI
class CrisisEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit CrisisEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class CrisisCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	explicit CrisisCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override { submitted.push_back(action); }
};
#endif
}

TEST_F(NewHorizonsCrisisCommandTest, EnemyKillImmediatelyRoutesDefenderFreeOrderThenResumesCompletedAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto heroBefore = battle()->battleHeroActionAllowanceCounts(recipient).heroActions;
	ASSERT_TRUE(kill());
	ASSERT_FALSE(victim->alive());
	ASSERT_TRUE(battle()->getCrisisCommandState().choice());
	EXPECT_EQ(battle()->getCrisisCommandState().chooser(), recipient);
	EXPECT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::CRISIS_ORDER);
	EXPECT_FALSE(battle()->battleBeginsActivation(battle()->battleActiveUnit(), BattleUnitTurnReason::CRISIS_ORDER));
	const auto attacks = server.attacks.size();
	ASSERT_TRUE(issue());
	EXPECT_FALSE(battle()->getCrisisCommandState().choice());
	EXPECT_TRUE(battle()->getCrisisCommandState().returns.empty());
	EXPECT_EQ(server.attacks.size(), attacks) << "Resumption must not replay the killing action";
	EXPECT_EQ(battle()->battleHeroActionAllowanceCounts(recipient).heroActions, heroBefore);
	EXPECT_TRUE(battle()->battleGetHeroOrderState(recipient, HeroCommand::HOLD_THE_LINE));
	EXPECT_TRUE(std::any_of(server.stackActivations.begin(), server.stackActivations.end(), [](const auto & pack)
		{ return pack.reason == BattleUnitTurnReason::CRISIS_RESUME; }));
}

TEST_F(NewHorizonsCrisisCommandTest, RejectedCreatureSpellAndWrongSideRequestsPreserveExactFreeChoice)
{
	ASSERT_NO_FATAL_FAILURE(prepare()); ASSERT_TRUE(kill());
	const auto grants = battle()->getHeroActionAllowances(recipient);
	const auto * anchor = battle()->battleGetStackByID(battle()->getCrisisCommandState().returns.back().anchor);
	ASSERT_FALSE(action(anchor));
	auto invalidDecline = BattleAction::makeNoAction(anchor); invalidDecline.side = BattleSide::ATTACKER;
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), invalidDecline));
	auto spell = BattleAction::makeHeroCommand(recipient, HeroCommand::HOLD_THE_LINE);
	spell.actionType = EActionType::HERO_SPELL; spell.spell = SpellID::HASTE;
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), spell));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE)));
	EXPECT_EQ(battle()->getHeroActionAllowances(recipient), grants);
	EXPECT_TRUE(battle()->getCrisisCommandState().choice());
	ASSERT_TRUE(issue());
}

TEST_F(NewHorizonsCrisisCommandTest, DeclineSpendsNoHeroActionAndDoesNotActWithTheTemporaryAnchor)
{
	ASSERT_NO_FATAL_FAILURE(prepare()); ASSERT_TRUE(kill());
	const auto * anchor = battle()->battleActiveUnit();
	const auto moved = anchor->moved(); const auto waited = anchor->waited();
	const auto actions = server.startedActions.size();
	const auto hero = battle()->battleHeroActionAllowanceCounts(recipient).heroActions;
	ASSERT_TRUE(decline());
	EXPECT_EQ(server.startedActions.size(), actions);
	EXPECT_EQ(anchor->moved(), moved); EXPECT_EQ(anchor->waited(), waited);
	EXPECT_EQ(battle()->battleHeroActionAllowanceCounts(recipient).heroActions, hero);
	EXPECT_TRUE(battle()->getCrisisCommandState().used.at(1));
	EXPECT_TRUE(battle()->getCrisisCommandState().returns.empty());
}

TEST_F(NewHorizonsCrisisCommandTest, FreeSecondWindRunsBeforeOriginalFlowWithoutDiscardingItsExtra)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	activate(reserve); ASSERT_TRUE(action(reserve)); activate(actor);
	// Accepted Defend spends the activation without setting movedThisRound;
	// canonical Second Wind explicitly accepts this defending provenance.
	ASSERT_TRUE(reserve->defended()); ASSERT_TRUE(kill());
	// battleCanConfirmHeroCommand is the Focus Fire confirmation interface;
	// targeted canonical Orders use the complete prepared-state validation.
	ASSERT_EQ(battle()->battleGetHeroOrderTargetRejection(recipient, HeroCommand::SECOND_WIND,
		{reserve->unitId()}), heroCommands::TargetRejection::NONE);
	ASSERT_TRUE(battle()->battlePrepareHeroOrderState(recipient, HeroCommand::SECOND_WIND,
		{reserve->unitId()}).has_value());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeTargetedHeroCommand(recipient, HeroCommand::SECOND_WIND, reserve->unitId())));
	ASSERT_EQ(battle()->battleActiveUnit(), reserve);
	ASSERT_EQ(battle()->getCrisisCommandState().returns.size(), 1u);
	EXPECT_EQ(battle()->getCrisisCommandState().returns.back().phase, newHorizonsCrisisCommand::Phase::GRANTED_EXTRA);
	const auto attacks = server.attacks.size();
	ASSERT_TRUE(action(reserve));
	EXPECT_TRUE(battle()->getCrisisCommandState().returns.empty());
	EXPECT_EQ(server.attacks.size(), attacks);
	const auto secondWind = battle()->battleGetHeroOrderState(recipient, HeroCommand::SECOND_WIND);
	ASSERT_TRUE(secondWind); EXPECT_FALSE(secondWind->secondWindActive);
}

TEST_F(NewHorizonsCrisisCommandTest, StoppedAnchorRoutesOnlyHeroChoiceWithoutStartingOrThawingItsActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// The stopped stack remains the lowest-ID input anchor, but cannot itself
	// receive Hold while outside time. Keep a genuine eligible Order recipient.
	auto * recipientUnit = addStack(recipient, creatureByName("core:pikeman"), BattleHex(12, 2), 100);
	ASSERT_GT(recipientUnit->unitId(), reserve->unitId());
	SetStackEffect stop; stop.battleID = BattleID(0);
	stop.toAdd.emplace_back(reserve->unitId(), std::vector<Bonus>{Bonus(BonusDuration::ONE_BATTLE,
		BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID())});
	gameHandler->sendAndApply(stop);
	ASSERT_TRUE(reserve->isTimeStopped());
	const auto moved = reserve->moved();
	ASSERT_TRUE(kill());
	ASSERT_EQ(battle()->battleActiveUnit(), reserve);
	EXPECT_FALSE(battle()->battleBeginsActivation(reserve, BattleUnitTurnReason::CRISIS_ORDER));
	ASSERT_TRUE(issue());
	EXPECT_TRUE(reserve->isTimeStopped()); EXPECT_EQ(reserve->moved(), moved);
	EXPECT_TRUE(battle()->getCrisisCommandState().returns.empty());
}

TEST_F(NewHorizonsCrisisCommandTest, RetaliationDestroysOriginalActorAndFreeSecondWindStillReturnsSafely)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	activate(reserve); ASSERT_TRUE(action(reserve)); activate(actor);
	ASSERT_TRUE(kill()); ASSERT_FALSE(actor->alive());
	ASSERT_TRUE(battle()->getCrisisCommandState().choice());
	EXPECT_EQ(battle()->getCrisisCommandState().returns.back().originalActor, static_cast<int32_t>(actor->unitId()));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(recipient, HeroCommand::SECOND_WIND, reserve->unitId())));
	ASSERT_EQ(battle()->battleActiveUnit(), reserve); ASSERT_TRUE(action(reserve));
	EXPECT_TRUE(battle()->getCrisisCommandState().returns.empty()); EXPECT_FALSE(actor->alive());
	EXPECT_NE(battle()->getActiveStackID(), static_cast<int32_t>(actor->unitId()));
}

TEST_F(NewHorizonsCrisisCommandTest, WholeInjuryBatchCapturesTwoDeathsOnceAndNeverOpensMidSequence)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * extra = addStack(recipient, creatureByName("core:peasant"), BattleHex(12, 7), 1);
	const auto activations = server.stackActivations.size();
	injure({victim, extra}, reserve->unitId());
	ASSERT_FALSE(victim->alive()); ASSERT_FALSE(extra->alive());
	EXPECT_EQ(server.stackActivations.size(), activations);
	EXPECT_FALSE(battle()->getCrisisCommandState().choice());
	ASSERT_EQ(battle()->getCrisisCommandState().pending.size(), 1u);
	ASSERT_TRUE(action(actor)); ASSERT_TRUE(battle()->getCrisisCommandState().choice());
	ASSERT_TRUE(issue());
	EXPECT_EQ(battle()->battleHeroActionAllowanceCounts(recipient).heroActions, 1u);
}

TEST_F(NewHorizonsCrisisCommandTest, SummonedStackDestructionQualifiesButRemovalAloneDoesNot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	victim->summoned = true;
	BattleUnitsChanged remove; remove.battleID = BattleID(0);
	auto * temporary = addStack(recipient, creatureByName("core:peasant"), BattleHex(12, 7), 1);
	remove.changedStacks.emplace_back(temporary->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	EXPECT_FALSE(battle()->getCrisisCommandState().meaningful());
	ASSERT_TRUE(kill()); EXPECT_TRUE(battle()->getCrisisCommandState().choice());
}

TEST_F(NewHorizonsCrisisCommandTest, CurrentControllerIsCapturedBeforeDeathRemovesHypnosis)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(battle()->battleGetOwner(victim), PlayerColor(0));
	injure({victim}, actor->unitId());
	ASSERT_EQ(battle()->getCrisisCommandState().pending.size(), 1u);
	EXPECT_EQ(battle()->getCrisisCommandState().pending.front().side, BattleSide::ATTACKER);
	EXPECT_TRUE(battle()->getCrisisCommandState().used.at(0)); EXPECT_FALSE(battle()->getCrisisCommandState().used.at(1));
}

TEST_F(NewHorizonsCrisisCommandTest, PuppetControllerReceivesDestructionReceiptWithoutChangingPhysicalAllegiance)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	const auto spell = SpellID(SpellID::decode(std::string(newHorizonsPuppetMaster::SPELL_ID)));
	SetStackEffect control; control.battleID = BattleID(0);
	control.toAdd.emplace_back(victim->unitId(), std::vector<Bonus>{newHorizonsPuppetMaster::controlMarker(spell, PlayerColor(0))});
	gameHandler->sendAndApply(control);
	ASSERT_EQ(battle()->battleGetOwner(victim), PlayerColor(1));
	ASSERT_EQ(battle()->battleGetActionController(victim), PlayerColor(0));
	injure({victim}, actor->unitId());
	ASSERT_EQ(battle()->getCrisisCommandState().pending.size(), 1u);
	EXPECT_EQ(battle()->getCrisisCommandState().pending.front().side, BattleSide::ATTACKER);
	EXPECT_TRUE(battle()->getCrisisCommandState().used.at(0)); EXPECT_FALSE(battle()->getCrisisCommandState().used.at(1));
}

TEST_F(NewHorizonsCrisisCommandTest, UnselectedCapturedPerkDoesNotGrantAFreeOrder)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false)); ASSERT_TRUE(kill());
	EXPECT_FALSE(battle()->getCrisisCommandState().meaningful());
	EXPECT_FALSE(std::any_of(server.stackActivations.begin(), server.stackActivations.end(), [](const auto & packet)
		{ return packet.reason == BattleUnitTurnReason::CRISIS_ORDER; }));
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

TEST_F(NewHorizonsCrisisCommandTest, SuspendedMetadataRoundTripsAndOlderBattleStartRejectsBeforeItsPrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare()); ASSERT_TRUE(kill());
	CMemorySerializer metadata;
	auto state = battle()->getCrisisCommandState(); metadata.oser & state;
	newHorizonsCrisisCommand::State restored; metadata.iser & restored;
	ASSERT_EQ(restored.returns.size(), 1u);
	EXPECT_TRUE(restored.returns.front().sameContinuation(state.returns.front()));
	EXPECT_EQ(restored.used, state.used); restored.validate(*battle());
	// Descriptor-only roundtrip deliberately does not claim restored live HP.
	auto descriptor = battleStartFixture::snapshot(*battle(), gameState().get());
	ASSERT_EQ(descriptor->getCrisisCommandState().returns.size(), 1u);
	EXPECT_EQ(descriptor->getCrisisCommandState().returns.front().originalActor, state.returns.front().originalActor);
	CrisisOldPrefix old;
	BattleStart start; start.info = std::move(descriptor);
	EXPECT_THROW(start.serialize(old), std::runtime_error); EXPECT_EQ(old.fields, 0);
}

TEST_F(NewHorizonsCrisisCommandTest, CurrentDescriptorDecodeRejectsMissingSuspensionReference)
{
	ASSERT_NO_FATAL_FAILURE(prepare()); ASSERT_TRUE(kill());
	const auto original = battle()->getCrisisCommandState();
	CMemorySerializer wire;
	CrisisDescriptorCorruptor writer(wire.oser, original.returns.back().anchor, true);
	ASSERT_NO_THROW(battle()->serialize(writer));
	wire.iser.cb = gameState().get();
	auto decoded = std::make_unique<BattleInfo>(gameState().get());
	EXPECT_THROW(wire.iser & *decoded, std::runtime_error);
	EXPECT_TRUE(battle()->getCrisisCommandState().returns.back().sameContinuation(original.returns.back()));
}

TEST_F(NewHorizonsCrisisCommandTest, DescriptorReferencesDoNotWaiveImpossibleLiveAnchorAfterBinding)
{
	ASSERT_NO_FATAL_FAILURE(prepare()); ASSERT_TRUE(kill());
	const auto original = battle()->getCrisisCommandState();
	CMemorySerializer wire;
	CrisisDescriptorCorruptor writer(wire.oser, original.returns.back().anchor, false);
	ASSERT_NO_THROW(battle()->serialize(writer));
	wire.iser.cb = gameState().get();
	auto decoded = std::make_unique<BattleInfo>(gameState().get());
	ASSERT_NO_THROW(wire.iser & *decoded);
	EXPECT_NO_THROW(decoded->getCrisisCommandState().validateSerializedReferences(*decoded));
	auto restore = vstd::makeScopeGuard([&] { decoded.reset(); battle()->localInit(); });
	EXPECT_THROW(decoded->localInit(), std::runtime_error);
	EXPECT_TRUE(battle()->getCrisisCommandState().returns.back().sameContinuation(original.returns.back()));
}

TEST_F(NewHorizonsCrisisCommandTest, ActiveProfileRejectsBeforeSettingsHeroMapWorldAndLobbyPrefixes)
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

TEST(NewHorizonsCrisisCommandProtocolTest, PlannedAndAbsentProfilesRemainOldCompatibleWhileMalformedAndRawActiveDoNot)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
	for(auto & perk : rules["skills"][newHorizonsCrisisCommand::SKILL]["perks"].Vector())
		if(perk["id"].String() == newHorizonsCrisisCommand::PERK) perk["effect"]["status"].String() = "planned";
	EXPECT_NO_THROW(newHorizonsCrisisCommand::validateProfileSerialization(JsonNode(), false));
	EXPECT_NO_THROW(newHorizonsCrisisCommand::validateProfileSerialization(rules, false));
	newHorizonsHeroes::PerkState original; original.rules = rules;
	CMemorySerializer compatible; compatible.oser.version = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_CRISIS_COMMAND) - 1);
	EXPECT_NO_THROW(compatible.oser & original);
	for(auto & perk : rules["skills"][newHorizonsCrisisCommand::SKILL]["perks"].Vector())
		if(perk["id"].String() == newHorizonsCrisisCommand::PERK) perk["effect"]["status"].String() = "active";
	original.rules = rules;
	CMemorySerializer raw; raw.oser & original;
	raw.iser.version = compatible.oser.version;
	newHorizonsHeroes::PerkState rejected;
	EXPECT_THROW(raw.iser & rejected, std::runtime_error);
	for(auto & perk : rules["skills"][newHorizonsCrisisCommand::SKILL]["perks"].Vector())
		if(perk["id"].String() == newHorizonsCrisisCommand::PERK) perk["effect"]["status"].Bool() = true;
	EXPECT_THROW(newHorizonsCrisisCommand::activeProfile(rules), std::runtime_error);
}

TEST_F(NewHorizonsCrisisCommandTest, InvalidStateCannotRewriteHeroAllowanceOrSuspendedAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare()); ASSERT_TRUE(kill());
	const auto heroCount = battle()->battleHeroActionAllowanceCounts(recipient).heroActions;
	BattleCrisisCommandChanged forged; forged.battleID = BattleID(0);
	forged.state = battle()->getCrisisCommandState(); forged.state.returns.back().action.actionType = EActionType::WAIT;
	EXPECT_THROW(gameHandler->sendAndApply(forged), std::runtime_error);
	EXPECT_EQ(battle()->battleHeroActionAllowanceCounts(recipient).heroActions, heroCount);
	EXPECT_TRUE(battle()->getCrisisCommandState().choice());
}

TEST_F(NewHorizonsCrisisCommandTest, CrisisPacketsAndGrantRejectOlderWritersBeforeAnyPrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare()); ASSERT_TRUE(kill());
	BattleCrisisCommandChanged transition; transition.battleID = BattleID(0);
	transition.state = battle()->getCrisisCommandState();
	CrisisOldPrefix state;
	EXPECT_THROW(transition.serialize(state), std::runtime_error); EXPECT_EQ(state.fields, 0);
	BattleSetActiveStack anchor; anchor.battleID = BattleID(0);
	anchor.stack = transition.state.returns.back().anchor; anchor.reason = BattleUnitTurnReason::CRISIS_ORDER;
	CrisisOldPrefix activation;
	EXPECT_THROW(anchor.serialize(activation), std::runtime_error); EXPECT_EQ(activation.fields, 0);
	auto grant = *std::find_if(battle()->getHeroActionAllowances(recipient).grants.begin(),
		battle()->getHeroActionAllowances(recipient).grants.end(), [](const auto & value)
		{ return value.source == HeroActionAllowanceState::GrantSource::CRISIS_COMMAND; });
	CrisisOldPrefix allowance;
	EXPECT_THROW(grant.serialize(allowance), std::runtime_error); EXPECT_EQ(allowance.fields, 0);
}

TEST_F(NewHorizonsCrisisCommandTest, OppositeHolderDeathDuringGrantedExtraNestsThenResumesBothActionsExactlyOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	BattleUnitsChanged remove; remove.battleID = BattleID(0);
	remove.changedStacks.emplace_back(other->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	auto * fragile = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(3, 7), 1);
	activate(reserve); ASSERT_TRUE(action(reserve)); activate(actor);
	ASSERT_TRUE(kill());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeTargetedHeroCommand(BattleSide::DEFENDER, HeroCommand::SECOND_WIND, reserve->unitId())));
	ASSERT_EQ(battle()->battleActiveUnit(), reserve);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeShotAttack(reserve, fragile)));
	ASSERT_FALSE(fragile->alive());
	ASSERT_EQ(battle()->getCrisisCommandState().returns.size(), 2u);
	ASSERT_EQ(battle()->getCrisisCommandState().chooser(), BattleSide::ATTACKER);
	const auto attacks = server.attacks.size();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE)));
	EXPECT_TRUE(battle()->getCrisisCommandState().returns.empty());
	EXPECT_EQ(server.attacks.size(), attacks);
	EXPECT_TRUE(battle()->getCrisisCommandState().used.at(0)); EXPECT_TRUE(battle()->getCrisisCommandState().used.at(1));
}

TEST_F(NewHorizonsCrisisCommandTest, CloneDeathQualifiesAndLaterDestructionCannotReenterConsumedHero)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BattleUnitsChanged clone; clone.battleID = BattleID(0);
	clone.changedStacks.emplace_back(victim->unitId(), UnitChanges::EOperation::UPDATE);
	clone.changedStacks.back().data = victim->save(); clone.changedStacks.back().data["state"]["cloned"].Bool() = true;
	gameHandler->sendAndApply(clone);
	ASSERT_TRUE(kill()); ASSERT_TRUE(battle()->getCrisisCommandState().choice()); ASSERT_TRUE(decline());
	auto * later = addStack(recipient, creatureByName("core:peasant"), BattleHex(12, 7), 1);
	injure({later}, actor->unitId());
	EXPECT_TRUE(battle()->getCrisisCommandState().pending.empty());
	EXPECT_TRUE(battle()->getCrisisCommandState().returns.empty());
}

#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsCrisisCommandTest, DetachedFreeOrderProjectionUsesExactGrantWithoutSpendingOrMutatingLiveState)
{
	ASSERT_NO_FATAL_FAILURE(prepare()); ASSERT_TRUE(kill());
	CrisisEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto * rng = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator()); ASSERT_NE(rng, nullptr);
	const auto randomBefore = crisisRandomState(*rng);
	const auto heroCount = battle()->battleHeroActionAllowanceCounts(recipient).heroActions;
	const auto prepared = child->prepareHeroOrderAllowance(recipient); ASSERT_TRUE(prepared);
	EXPECT_EQ(prepared->action.receipt.source, HeroActionAllowanceState::GrantSource::CRISIS_COMMAND);
	ASSERT_TRUE(child->beginProjectedHeroAction(recipient, *prepared));
	ASSERT_TRUE(child->projectAcceptedHeroOrder(recipient, HeroCommand::HOLD_THE_LINE, {}, *prepared));
	EXPECT_FALSE(child->getCrisisCommandState().choice());
	EXPECT_TRUE(parent->getCrisisCommandState().choice()); EXPECT_TRUE(battle()->getCrisisCommandState().choice());
	EXPECT_EQ(child->getHeroActionAllowances(recipient).remainingCounts(child->getRound()).heroActions, heroCount);
	EXPECT_EQ(crisisRandomState(*rng), randomBefore);
}

TEST_F(NewHorizonsCrisisCommandTest, ActualDefendingAISelectsARealFreeOrderAndAuthorityResumesTheEnemyFlow)
{
	ASSERT_NO_FATAL_FAILURE(prepare()); ASSERT_TRUE(kill());
	auto callback = std::make_shared<CrisisCallback>(PlayerColor(1)); callback->onBattleStarted(battle());
	auto environment = std::make_shared<CrisisEnvironment>(gameState());
	const auto * anchor = battle()->battleGetStackByID(battle()->getCrisisCommandState().returns.back().anchor);
	const auto hp = reserve->getAvailableHealth();
	auto * rng = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator()); ASSERT_NE(rng, nullptr);
	const auto randomBefore = crisisRandomState(*rng);
	const auto heroCount = battle()->battleHeroActionAllowanceCounts(recipient).heroActions;
	BattleEvaluator evaluator(environment, callback, anchor, PlayerColor(1), BattleID(0), recipient, 1.0f, 2);
	evaluator.selectStackAction(anchor);
	ASSERT_TRUE(evaluator.attemptCastingSpell(anchor));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto chosen = callback->submitted.front(); ASSERT_EQ(chosen.actionType, EActionType::HERO_COMMAND);
	EXPECT_EQ(reserve->getAvailableHealth(), hp); EXPECT_TRUE(battle()->getCrisisCommandState().choice());
	EXPECT_EQ(crisisRandomState(*rng), randomBefore);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), chosen));
	EXPECT_EQ(battle()->battleHeroActionAllowanceCounts(recipient).heroActions, heroCount);
	if(chosen.command == HeroCommand::SECOND_WIND)
	{
		const auto * activeStack = battle()->battleGetStackByID(battle()->getActiveStackID());
		ASSERT_NE(activeStack, nullptr);
		ASSERT_TRUE(action(activeStack));
	}
	EXPECT_TRUE(battle()->getCrisisCommandState().returns.empty());
}
#endif

TEST_F(NewHorizonsCrisisCommandTest, SeizeInterruptedNormalContextSurvivesCurrentChoiceSnapshotAndDetachedReturn)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, true, true));
	const auto original = battle()->getSeizeInitiativeState();
	ASSERT_EQ(original.active, actor->unitId()); ASSERT_TRUE(original.activeNormal);
	ASSERT_TRUE(kill());
	const auto & frame = battle()->getCrisisCommandState().returns.back();
	ASSERT_NE(frame.anchor, actor->unitId());
	EXPECT_EQ(frame.suspendedSeizeActive, actor->unitId());
	EXPECT_TRUE(frame.suspendedSeizeActiveNormal);
	EXPECT_EQ(battle()->getSeizeInitiativeState().active, actor->unitId());
	auto descriptor = battleStartFixture::snapshot(*battle(), gameState().get());
	auto rebind = vstd::makeScopeGuard([&] { descriptor.reset(); battle()->localInit(); });
	ASSERT_TRUE(descriptor);
	EXPECT_EQ(descriptor->getActiveStackID(), frame.anchor);
	EXPECT_EQ(descriptor->getCrisisCommandState().returns.back().suspendedSeizeActive, actor->unitId());
	EXPECT_EQ(descriptor->getSeizeInitiativeState(), battle()->getSeizeInitiativeState());
	EXPECT_NO_THROW(newHorizonsSeizeInitiative::validateReferences(*descriptor, descriptor->getSeizeInitiativeState()));
	BattleCrisisCommandChanged forged; forged.battleID = BattleID(0);
	forged.state = battle()->getCrisisCommandState();
	forged.state.returns.back().suspendedSeizeActiveNormal = false;
	EXPECT_THROW(gameHandler->sendAndApply(forged), std::runtime_error);
	EXPECT_TRUE(battle()->getCrisisCommandState().returns.back().suspendedSeizeActiveNormal);
#if ENABLE_BATTLE_AI
	CrisisEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto prepared = child->prepareHeroOrderAllowance(recipient); ASSERT_TRUE(prepared);
	ASSERT_TRUE(child->beginProjectedHeroAction(recipient, *prepared));
	ASSERT_TRUE(child->projectAcceptedHeroOrder(recipient, HeroCommand::HOLD_THE_LINE, {}, *prepared));
	EXPECT_TRUE(child->getCrisisCommandState().returns.empty());
	EXPECT_EQ(child->getActiveStackID(), actor->unitId());
	EXPECT_EQ(child->getSeizeInitiativeState(), original);
	EXPECT_TRUE(parent->getCrisisCommandState().choice());
	EXPECT_TRUE(sibling->getCrisisCommandState().choice());
	EXPECT_EQ(parent->getSeizeInitiativeState(), original);
	EXPECT_TRUE(battle()->getCrisisCommandState().choice());
#endif
	ASSERT_TRUE(decline());
	EXPECT_TRUE(battle()->getSeizeInitiativeState().completed(actor->unitId()));
}

TEST_F(NewHorizonsCrisisCommandTest, CrisisSecondWindRestoresOriginalNormalSlotAndReleasesPaidSeizeAnchorOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, true, true));
	activate(reserve); ASSERT_TRUE(action(reserve)); activate(actor);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE)));
	const auto paid = battle()->getSeizeInitiativeState().sides.at(0);
	ASSERT_TRUE(paid.triggered); ASSERT_TRUE(paid.awaitingAnchor);
	ASSERT_EQ(paid.anchor, actor->unitId()); ASSERT_EQ(paid.recipient, other->unitId());
	ASSERT_TRUE(kill());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeTargetedHeroCommand(recipient, HeroCommand::SECOND_WIND, reserve->unitId())));
	ASSERT_EQ(battle()->battleActiveUnit(), reserve);
	ASSERT_FALSE(battle()->getSeizeInitiativeState().activeNormal);
	ASSERT_EQ(battle()->getSeizeInitiativeState().active, reserve->unitId());
	EXPECT_TRUE(battle()->getSeizeInitiativeState().sides.at(0).awaitingAnchor);
	EXPECT_TRUE(battle()->getCrisisCommandState().returns.back().suspendedSeizeActiveNormal);
	const auto attacks = server.attacks.size();
	ASSERT_TRUE(action(reserve));
	const auto & resumed = battle()->getSeizeInitiativeState();
	EXPECT_TRUE(battle()->getCrisisCommandState().returns.empty());
	EXPECT_TRUE(resumed.completed(actor->unitId()));
	EXPECT_TRUE(resumed.completed(reserve->unitId()));
	EXPECT_EQ(std::count(resumed.normalCompleted.begin(), resumed.normalCompleted.end(), actor->unitId()), 1);
	EXPECT_EQ(std::count(resumed.normalCompleted.begin(), resumed.normalCompleted.end(), reserve->unitId()), 1);
	EXPECT_TRUE(resumed.sides.at(0).triggered);
	EXPECT_FALSE(resumed.sides.at(0).awaitingAnchor);
	EXPECT_EQ(resumed.sides.at(0).anchor, SeizeInitiativeState::NO_UNIT);
	EXPECT_EQ(battle()->battleActiveUnit(), other);
	EXPECT_EQ(server.attacks.size(), attacks);
}
