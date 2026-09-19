/*
 * FocusFirePersistenceTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "FocusFireFixture.h"
#include "BattleStartSnapshotFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/Unit.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/mapObjects/army/CStackBasicDescriptor.h"

namespace
{
// Native malformed-reader fixture: real binary decoding for every field, then
// inject a null unit or omit a descriptor at the vector boundary. No disk-file claim.
class MalformedUnitBattleReader
{
	BinaryDeserializer & decoder;
	std::optional<uint32_t> omittedUnitId;
public:
	using Version = ESerializationVersion;
	static constexpr bool saving = false;
	bool injected = false;

	explicit MalformedUnitBattleReader(BinaryDeserializer & decoder, std::optional<uint32_t> omitted = std::nullopt)
		: decoder(decoder), omittedUnitId(omitted) {}

	bool hasFeature(Version version) const { return decoder.hasFeature(version); }

	template<typename T> MalformedUnitBattleReader & operator&(T & value)
	{
		decoder & value;
		return *this;
	}

	MalformedUnitBattleReader & operator&(std::vector<std::unique_ptr<CStack>> & units)
	{
		decoder & units;
		if(omittedUnitId)
		{
			const auto omitted = std::find_if(units.begin(), units.end(), [&](const auto & unit)
			{
				return unit->unitId() == *omittedUnitId;
			});
			if(omitted == units.end())
				throw std::runtime_error("Missing omission fixture unit");
			units.erase(omitted);
		}
		else
			units.emplace_back();
		injected = true;
		return *this;
	}
};
}

class FocusFirePersistenceTest : public FocusFireFixture {};

class LegacyCommandAllocationTest : public HeroCommandFixture
{
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		const JsonNode config(JsonPath::builtin("config/newHorizonsCombatV2"));
		auto rules = config["combat"]["heroCommands"];
		rules["schemaVersion"].Integer() = 1;
		rules["rulesetVersion"].Integer() = 1;
		rules["commands"].Struct().erase("focusFire");
		heroCommands::validateRules(rules);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, rules);
	}
};

TEST_F(LegacyCommandAllocationTest, ExistingSparseAdmissionAndSizePolicyAreNotReinterpreted)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	const auto count = battle()->stacks.size();
	battle::UnitInfo info;
	info.id = static_cast<uint32_t>(count + 1);
	info.type = creatureByName("core:angel");
	info.count = 1;
	info.side = BattleSide::ATTACKER;
	info.position = BattleHex(leftHex + 2 * GameConstants::BFIELD_WIDTH);
	info.summoned = true;
	JsonNode data;
	info.save(data);
	ASSERT_NO_THROW(battle()->addUnit(info.id, data));
	EXPECT_NO_THROW(battle()->validateFocusFireStates());
	EXPECT_EQ(battle()->nextUnitId(), count + 1);
	// Deliberate legacy characterization, NOT a safe-to-use allocation recommendation.
	EXPECT_NE(battle()->battleGetUnitByID(battle()->nextUnitId()), nullptr);
	CMemorySerializer wire;
	EXPECT_NO_THROW(battleStartFixture::writeDescriptors(*battle(), wire));
}

TEST_F(FocusFirePersistenceTest, FullBattleStartRoundTripPreservesTargetCohortBudgetAndExpiry)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto beforeBattle = gameState()->saveToMemory();
	ASSERT_NO_FATAL_FAILURE(prepareFocusBattle());
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	const auto expected = battle()->battleGetFocusFireState(BattleSide::ATTACKER);
	ASSERT_TRUE(expected);
	auto replica = std::make_shared<CGameState>();
	replica->preInit(LIBRARY);
	replica->loadFromMemory(beforeBattle);
	BattleStart outgoing;
	outgoing.battleID = BattleID(0);
	outgoing.info = battleStartFixture::snapshot(*battle(), replica.get());
	CMemorySerializer wire;
	wire.oser & outgoing;
	wire.iser.cb = replica.get();
	BattleStart incoming;
	wire.iser & incoming;
	RecordingGameServer restoredServer;
	restoredServer.gameState = replica;
	auto handler = std::make_shared<CGameHandler>(restoredServer, replica);
	handler->sendAndApply(incoming);
	auto * restored = replica->getBattle(BattleID(0));
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->getHeroCommandRules(), battle()->getHeroCommandRules());
	EXPECT_EQ(restored->battleGetFocusFireState(BattleSide::ATTACKER), expected);
	EXPECT_TRUE(restored->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_FALSE(restored->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
	EXPECT_EQ(restored->battleTargetedRangedCommandPercent(restored->battleGetUnitByID(shooter->unitId()),
		restored->battleGetUnitByID(target->unitId()), true), 30);

	const auto addDescriptor = [&](uint32_t id)
	{
		battle::UnitInfo info;
		info.id = id;
		info.type = creatureByName("core:angel");
		info.count = 1;
		info.side = BattleSide::ATTACKER;
		info.position = BattleHex(leftHex + 2 * GameConstants::BFIELD_WIDTH);
		info.summoned = true;
		BattleUnitsChanged add;
		add.battleID = BattleID(0);
		add.changedStacks.emplace_back(id, UnitChanges::EOperation::ADD);
		info.save(add.changedStacks.back().data);
		handler->sendAndApply(add);
	};
	const auto count = restored->stacks.size();
	EXPECT_THROW(addDescriptor(target->unitId()), std::runtime_error);
	EXPECT_THROW(addDescriptor(restored->nextUnitId() + 1), std::runtime_error);
	EXPECT_EQ(restored->stacks.size(), count);
	const auto firstId = restored->nextUnitId();
	ASSERT_EQ(restored->battleGetUnitByID(firstId), nullptr);
	ASSERT_NO_THROW(addDescriptor(firstId));
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	remove.changedStacks.emplace_back(firstId, UnitChanges::EOperation::REMOVE);
	handler->sendAndApply(remove);
	ASSERT_NE(restored->battleGetUnitByID(firstId), nullptr);
	EXPECT_TRUE(restored->battleGetUnitByID(firstId)->isGhost());
	const auto secondId = restored->nextUnitId();
	EXPECT_NE(secondId, firstId);
	ASSERT_EQ(restored->battleGetUnitByID(secondId), nullptr);
	ASSERT_NO_THROW(addDescriptor(secondId));
	EXPECT_EQ(restored->stacks.size(), count + 2);
	EXPECT_EQ(restored->battleGetFocusFireState(BattleSide::ATTACKER), expected);
	EXPECT_TRUE(restored->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_EQ(restored->battleTargetedRangedCommandPercent(restored->battleGetUnitByID(shooter->unitId()),
		restored->battleGetUnitByID(target->unitId()), true), 30);
	EXPECT_NO_THROW(restored->validateFocusFireStates());
	BattleNextRound next;
	next.battleID = BattleID(0);
	handler->sendAndApply(next);
	EXPECT_FALSE(restored->battleGetFocusFireState(BattleSide::ATTACKER));
	EXPECT_TRUE(restored->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
	EXPECT_EQ(battle()->battleGetFocusFireState(BattleSide::ATTACKER), expected);
	EXPECT_EQ(attackerSideHero->battle, battle());
}

TEST_F(FocusFirePersistenceTest, OldFormatRefusesLossyTargetedWritesAndClearsAbsentFieldOnLegacyRead)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	const auto mark = battle()->battleGetFocusFireState(BattleSide::ATTACKER);
	ASSERT_TRUE(mark);
	CMemorySerializer sideWire;
	sideWire.oser.version = ESerializationVersion::NEW_HORIZONS_LOGISTICS_MASTERIES;
	EXPECT_THROW(sideWire.oser & battle()->getSide(BattleSide::ATTACKER), std::runtime_error);
	EXPECT_TRUE(sideWire.extractBuffer().empty());
	CMemorySerializer actionWire;
	actionWire.oser.version = ESerializationVersion::NEW_HORIZONS_LOGISTICS_MASTERIES;
	auto action = focusAction(target->unitId());
	EXPECT_THROW(actionWire.oser & action, std::runtime_error);
	EXPECT_TRUE(actionWire.extractBuffer().empty());
	CMemorySerializer battleWire;
	battleWire.oser.version = ESerializationVersion::NEW_HORIZONS_LOGISTICS_MASTERIES;
	EXPECT_THROW(battleWire.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(battleWire.extractBuffer().empty());

	SideInBattle legacy(gameState().get());
	legacy.heroCommandUsed = true;
	legacy.activeOrder = HeroCommand::CHARGE;
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_LOGISTICS_MASTERIES;
	old.iser.version = ESerializationVersion::NEW_HORIZONS_LOGISTICS_MASTERIES;
	old.oser & legacy;
	SideInBattle restored(gameState().get());
	restored.focusFire = mark;
	old.iser & restored;
	EXPECT_FALSE(restored.focusFire);
	EXPECT_TRUE(restored.heroCommandUsed);
	EXPECT_EQ(restored.activeOrder, HeroCommand::CHARGE);
}

TEST_F(FocusFirePersistenceTest, InvalidSavedContextCannotBeSerializedAsCanonicalBattle)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	const auto original = side;
	const std::vector<std::function<void(SideInBattle &)>> corruptions = {
		[](SideInBattle & value) { value.heroCommandUsed = false; },
		[](SideInBattle & value) { value.castSpellsCount = 1; },
		[](SideInBattle & value) { value.heroID = ObjectInstanceID(); },
		[](SideInBattle & value) { value.activeOrder = HeroCommand::CHARGE; },
		[](SideInBattle & value) { value.focusFire->issuedRound++; },
		[](SideInBattle & value) { value.focusFire->targetUnitId = std::numeric_limits<int32_t>::max(); },
		[](SideInBattle & value)
		{
			value.focusFire->recipientUnitIds = {static_cast<uint32_t>(std::numeric_limits<int32_t>::max())};
		}
	};
	for(size_t i = 0; i < corruptions.size(); ++i)
	{
		SCOPED_TRACE(i);
		side = original;
		corruptions[i](side);
		EXPECT_THROW(battle()->validateFocusFireStates(), std::runtime_error);
		CMemorySerializer rejected;
		EXPECT_THROW(rejected.oser & *battle(), std::runtime_error);
		EXPECT_TRUE(rejected.extractBuffer().empty());
	}
	side = original;
	EXPECT_NO_THROW(battle()->validateFocusFireStates());
	EXPECT_EQ(battle()->battleGetFocusFireState(BattleSide::ATTACKER), original.focusFire);
}

TEST_F(FocusFirePersistenceTest, TargetedRulesRejectUnsafeUnitIdsWithoutAnActiveMark)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	ASSERT_FALSE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
	CStackBasicDescriptor descriptor(creatureByName("core:angel"), 1);
	for(const int id : {static_cast<int>(target->unitId()), std::numeric_limits<int32_t>::min(),
		static_cast<int>(battle()->stacks.size() + 1)})
	{
		SCOPED_TRACE(id);
		auto invalid = std::make_unique<CStack>(&descriptor, PlayerColor(1), id, BattleSide::DEFENDER);
		invalid->initialPosition = BattleHex(rightHex + 2 * GameConstants::BFIELD_WIDTH);
		battle()->stacks.push_back(std::move(invalid));
		battle()->stacks.back()->localInit(battle());
		EXPECT_THROW(battle()->validateFocusFireStates(), std::runtime_error);
		CMemorySerializer rejected;
		EXPECT_THROW(rejected.oser & *battle(), std::runtime_error);
		EXPECT_TRUE(rejected.extractBuffer().empty());
		battle()->stacks.pop_back();
		EXPECT_NO_THROW(battle()->validateFocusFireStates());
	}
	EXPECT_TRUE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
}

TEST_F(FocusFirePersistenceTest, SparseDescriptorLoadRejectsAllocatorCollisionBeforePostBinding)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	ASSERT_FALSE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
	const auto omitted = std::find_if(battle()->stacks.begin(), battle()->stacks.end(), [&](const auto & unit)
	{
		return unit->unitId() != shooter->unitId() && unit->unitId() != target->unitId();
	});
	ASSERT_NE(omitted, battle()->stacks.end());
	ASSERT_LT((*omitted)->unitId(), battle()->stacks.size() - 1);
	CMemorySerializer wire;
	battleStartFixture::writeDescriptors(*battle(), wire);
	wire.iser.cb = gameState().get();
	MalformedUnitBattleReader reader(wire.iser, (*omitted)->unitId());
	{
		BattleInfo incoming(gameState().get());
		try
		{
			incoming.serialize(reader);
			ADD_FAILURE() << "Sparse descriptors admitted with a colliding size-based allocator";
		}
		catch(const std::runtime_error & error)
		{
			EXPECT_STREQ(error.what(), "Invalid New Horizons targeted battle unit identity");
		}
		EXPECT_TRUE(reader.injected);
	}
	EXPECT_EQ(attackerSideHero->battle, battle());
	EXPECT_EQ(defenderSideHero->battle, battle());
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_FALSE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
}

TEST_F(FocusFirePersistenceTest, DetachedSnapshotDestructionCannotClearAnotherBattleArmyBindings)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	const auto mark = battle()->battleGetFocusFireState(BattleSide::ATTACKER);
	ASSERT_EQ(attackerSideHero->battle, battle());
	ASSERT_EQ(defenderSideHero->battle, battle());
	for(const bool reject : {false, true})
	{
		SCOPED_TRACE(reject);
		// Deliberately bind a detached snapshot's IDs to the same world. It never
		// acquired the armies' battle pointers and must not clear their real owner.
		auto detached = battleStartFixture::snapshot(*battle(), gameState().get());
		ASSERT_NE(detached.get(), battle());
		EXPECT_EQ(attackerSideHero->battle, battle());
		if(reject)
		{
			detached->getSide(BattleSide::ATTACKER).focusFire->targetUnitId = std::numeric_limits<uint32_t>::max();
			EXPECT_THROW(detached->validateFocusFireStates(), std::runtime_error);
		}
		else
			EXPECT_NO_THROW(detached->validateFocusFireStates());
		detached.reset();
		EXPECT_EQ(attackerSideHero->battle, battle());
		EXPECT_EQ(defenderSideHero->battle, battle());
	}
	EXPECT_EQ(attackerSideHero->battle, battle());
	EXPECT_EQ(defenderSideHero->battle, battle());
	EXPECT_EQ(battle()->battleGetFocusFireState(BattleSide::ATTACKER), mark);
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(shooter, target, true), 30);
}

TEST_F(FocusFirePersistenceTest, DestroyingActualOwnerClearsItsArmyBindingsOnly)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto beforeBattle = gameState()->saveToMemory();
	ASSERT_NO_FATAL_FAILURE(prepareFocusBattle());
	auto replica = std::make_shared<CGameState>();
	replica->preInit(LIBRARY);
	replica->loadFromMemory(beforeBattle);
	auto owned = battleStartFixture::snapshot(*battle(), replica.get());
	auto * attackerArmy = owned->battleGetArmyObject(BattleSide::ATTACKER);
	auto * defenderArmy = owned->battleGetArmyObject(BattleSide::DEFENDER);
	ASSERT_NE(attackerArmy, nullptr);
	ASSERT_NE(defenderArmy, nullptr);
	ASSERT_EQ(attackerArmy->battle, nullptr);
	ASSERT_EQ(defenderArmy->battle, nullptr);
	owned->localInit();
	ASSERT_EQ(attackerArmy->battle, owned.get());
	ASSERT_EQ(defenderArmy->battle, owned.get());
	owned.reset();
	EXPECT_EQ(attackerArmy->battle, nullptr);
	EXPECT_EQ(defenderArmy->battle, nullptr);
	EXPECT_EQ(attackerSideHero->battle, battle());
	EXPECT_EQ(defenderSideHero->battle, battle());
}

TEST_F(FocusFirePersistenceTest, MalformedReaderIsRejectedBeforePostBindingAndPreservesLiveBattle)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	CMemorySerializer wire;
	battleStartFixture::writeDescriptors(*battle(), wire);
	wire.iser.cb = gameState().get();
	MalformedUnitBattleReader reader(wire.iser);
	{
		BattleInfo incoming(gameState().get());
		try
		{
			incoming.serialize(reader);
			ADD_FAILURE() << "Null unit reached post-binding without rejection";
		}
		catch(const std::runtime_error & error)
		{
			EXPECT_STREQ(error.what(), "Invalid null battle unit reference");
		}
		EXPECT_TRUE(reader.injected); // Actual BattleInfo loading branch reached the unit-vector field.
		EXPECT_EQ(attackerSideHero->battle, battle());
		EXPECT_EQ(defenderSideHero->battle, battle());
	}
	EXPECT_EQ(attackerSideHero->battle, battle()); // Rejected object's teardown cannot clear another owner.
	EXPECT_EQ(defenderSideHero->battle, battle());
	EXPECT_TRUE(battle()->battleIsFocusFireTargetActive(BattleSide::ATTACKER));
}

TEST_F(FocusFirePersistenceTest, ExpiredMarkDoesNotPermitDiscardingV2BattleRules)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	BattleNextRound next;
	next.battleID = BattleID(0);
	gameHandler->sendAndApply(next);
	ASSERT_FALSE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
	ASSERT_EQ(battle()->getActiveOrder(BattleSide::ATTACKER), HeroCommand::NONE);
	CMemorySerializer rejected;
	rejected.oser.version = ESerializationVersion::NEW_HORIZONS_LOGISTICS_MASTERIES;
	EXPECT_THROW(rejected.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(rejected.extractBuffer().empty());
}

TEST_F(FocusFirePersistenceTest, MalformedInternalBattleStartIsRejectedBeforeArmyAttachment)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto beforeBattle = gameState()->saveToMemory();
	ASSERT_NO_FATAL_FAILURE(prepareFocusBattle());
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	auto replica = std::make_shared<CGameState>();
	replica->preInit(LIBRARY);
	replica->loadFromMemory(beforeBattle);
	BattleStart malformed;
	malformed.battleID = BattleID(0);
	malformed.info = battleStartFixture::snapshot(*battle(), replica.get());
	malformed.info->getSide(BattleSide::ATTACKER).focusFire->targetUnitId = std::numeric_limits<uint32_t>::max();
	auto * hero = replica->getHero(attackerSideHero->id);
	ASSERT_NE(hero, nullptr);
	ASSERT_EQ(hero->battle, nullptr);
	const auto mana = hero->mana;
	RecordingGameServer restoredServer;
	restoredServer.gameState = replica;
	auto handler = std::make_shared<CGameHandler>(restoredServer, replica);
	EXPECT_THROW(handler->sendAndApply(malformed), std::runtime_error);
	EXPECT_TRUE(replica->currentBattles.empty());
	EXPECT_EQ(hero->battle, nullptr);
	EXPECT_EQ(hero->mana, mana);
	// A null unit must be rejected before localInit/post-load consumers dereference it.
	malformed.info->getSide(BattleSide::ATTACKER).focusFire->targetUnitId = target->unitId();
	malformed.info->stacks.emplace_back();
	EXPECT_THROW(handler->sendAndApply(malformed), std::runtime_error);
	EXPECT_TRUE(replica->currentBattles.empty());
	EXPECT_EQ(hero->battle, nullptr);
	EXPECT_EQ(hero->mana, mana);
	EXPECT_EQ(attackerSideHero->battle, battle());
}
