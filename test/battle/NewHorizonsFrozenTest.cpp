/*
 * NewHorizonsFrozenTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../mock/mock_BonusBearer.h"
#include "../mock/mock_UnitEnvironment.h"
#include "../mock/mock_UnitInfo.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/PhysicalAffliction.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include <limits>

namespace
{
constexpr auto preFrozenVersion = ESerializationVersion::NEW_HORIZONS_REALITY_WARP_EXCHANGE;

template<typename Packet>
void rejectsOlderWriterBeforePrefix(Packet & packet)
{
	CMemorySerializer memory;
	memory.oser.version = preFrozenVersion;
	EXPECT_THROW(packet.serialize(memory.oser), std::runtime_error);
	EXPECT_TRUE(memory.extractBuffer().empty());
}

// World/battle preflight must run before even the first serialized member.
// Avoid traversing an unrelated entire world merely to test that boundary.
struct PrefixProbe
{
	using Version = ESerializationVersion;
	bool saving = true;
	bool loadingGamestate = false;
	int fields = 0;
	bool hasFeature(Version version) const { return version != Version::NEW_HORIZONS_FROZEN; }
	template<typename T> PrefixProbe & operator&(T &)
	{
		++fields;
		throw std::runtime_error("Reached snapshot payload");
	}
};

class NewHorizonsFrozenSnapshotTest : public HeroCommandFixture
{
protected:
	bool includeCreatureRules = true;
	bool zeroCreatureChance = false;
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		if(!includeCreatureRules)
			rules.Struct().erase("creatureAbilities");
		else if(zeroCreatureChance)
			rules["creatureAbilities"]["freezingTouchChancePercent"].Integer() = 0;
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}
};

TEST_F(NewHorizonsFrozenSnapshotTest, CurrentActiveAndCleansedStackReceiptsRoundTripAndSurviveRealRebind)
{
	startGame();
	startBattle();
	for(const bool active : {false, true})
	{
		CStackBasicDescriptor descriptor(CreatureID(0), 3);
		CStack original(&descriptor, PlayerColor(0), 70, BattleSide::ATTACKER);
		original.initialPosition = BattleHex(leftHex);
		original.restoreFrozenApplicationRound(2);
		if(active)
			original.addNewBonus(std::make_shared<Bonus>(newHorizonsFrozen::marker(BonusSourceID(CreatureID(0)), 2)));
		CMemorySerializer memory;
		ASSERT_NO_THROW(original.serialize(memory.oser));
		CStack decoded;
		ASSERT_NO_THROW(decoded.serialize(memory.iser));
		EXPECT_EQ(decoded.frozenLastAppliedRound(), 2);
		// CStack's native/local bonus graph is rebuilt by real localInit, rather
		// than treating descriptor deserialization as an already rebound unit.
		EXPECT_EQ(decoded.getExportedBonusList().size(), active ? 1u : 0u);
		decoded.localInit(battle());
		EXPECT_EQ(decoded.frozenLastAppliedRound(), 2);
		EXPECT_EQ(decoded.isNewHorizonsFrozen(), active);
		EXPECT_FALSE(newHorizonsFrozen::canApply(decoded, 2));
		EXPECT_EQ(decoded.getCount(), 3);
		decoded.detachFromAll();
		rejectsOlderWriterBeforePrefix(original);
	}
}

TEST_F(NewHorizonsFrozenSnapshotTest, OrdinaryStackOlderRoundTripDefaultsToAbsentReceipt)
{
	CStackBasicDescriptor descriptor(CreatureID(0), 3);
	CStack original(&descriptor, PlayerColor(0), 70, BattleSide::ATTACKER);
	original.initialPosition = BattleHex(leftHex);
	CMemorySerializer memory;
	memory.oser.version = memory.iser.version = preFrozenVersion;
	ASSERT_NO_THROW(original.serialize(memory.oser));
	CStack decoded;
	ASSERT_NO_THROW(decoded.serialize(memory.iser));
	EXPECT_EQ(decoded.frozenLastAppliedRound(), -1);
	EXPECT_FALSE(decoded.isNewHorizonsFrozen());
}

TEST_F(NewHorizonsFrozenSnapshotTest, CapturedCreatureRulesRejectBeforeWorldAndBattleSnapshotPrefixes)
{
	zeroCreatureChance = true;
	startGame();
	startBattle();
	ASSERT_TRUE(gameState()->getMagicRules().Struct().count("creatureAbilities"));
	ASSERT_TRUE(battle()->getMagicRules().Struct().count("creatureAbilities"));
	ASSERT_EQ(newHorizonsFrozen::chancePercent(battle()->getMagicRules()), 0);
	PrefixProbe world;
	EXPECT_THROW(gameState()->serialize(world), std::runtime_error);
	EXPECT_EQ(world.fields, 0);
	PrefixProbe combat;
	EXPECT_THROW(battle()->serialize(combat), std::runtime_error);
	EXPECT_EQ(combat.fields, 0);
}

TEST_F(NewHorizonsFrozenSnapshotTest, OldAbsentCreatureRulesReachPayloadWithoutInventingSettings)
{
	includeCreatureRules = false;
	startGame();
	startBattle();
	ASSERT_EQ(gameState()->getMagicRules().Struct().count("creatureAbilities"), 0u);
	ASSERT_EQ(battle()->getMagicRules().Struct().count("creatureAbilities"), 0u);
	PrefixProbe world;
	EXPECT_THROW(gameState()->serialize(world), std::runtime_error);
	EXPECT_EQ(world.fields, 1);
	PrefixProbe combat;
	EXPECT_THROW(battle()->serialize(combat), std::runtime_error);
	EXPECT_EQ(combat.fields, 1);
	EXPECT_EQ(gameState()->getMagicRules().Struct().count("creatureAbilities"), 0u);
	EXPECT_EQ(battle()->getMagicRules().Struct().count("creatureAbilities"), 0u);
}

TEST_F(NewHorizonsFrozenSnapshotTest, BattleReceiptRejectsOlderSnapshotAfterMarkerCleansingToo)
{
	includeCreatureRules = false;
	startGame();
	startBattle();
	auto * recipient = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 3);
	ASSERT_NE(recipient, nullptr);
	const auto marker = newHorizonsFrozen::marker(BonusSourceID(CreatureID(0)), 2);
	battle()->addUnitBonus(recipient->unitId(), {marker});
	ASSERT_TRUE(recipient->isNewHorizonsFrozen());
	PrefixProbe active;
	EXPECT_THROW(battle()->serialize(active), std::runtime_error);
	EXPECT_EQ(active.fields, 0);
	battle()->removeUnitBonus(recipient->unitId(), {marker});
	ASSERT_FALSE(recipient->isNewHorizonsFrozen());
	ASSERT_EQ(recipient->frozenLastAppliedRound(), 2);
	PrefixProbe cleansed;
	EXPECT_THROW(battle()->serialize(cleansed), std::runtime_error);
	EXPECT_EQ(cleansed.fields, 0);
}

TEST_F(NewHorizonsFrozenSnapshotTest, BattleStartRejectsCapturedZeroChanceRulesBeforeItsOwnPrefix)
{
	zeroCreatureChance = true;
	startGame();
	BattleStart packet;
	packet.battleID = BattleID(0);
	packet.info = std::make_unique<BattleInfo>(gameState().get());
	ASSERT_TRUE(packet.info->getMagicRules().Struct().count("creatureAbilities"));
	ASSERT_EQ(newHorizonsFrozen::chancePercent(packet.info->getMagicRules()), 0);
	rejectsOlderWriterBeforePrefix(packet);
}

TEST_F(NewHorizonsFrozenSnapshotTest, BattleStartRejectsCleansedReceiptBeforeItsOwnPrefixWithoutCreatureRules)
{
	includeCreatureRules = false;
	startGame();
	BattleStart packet;
	packet.battleID = BattleID(0);
	packet.info = std::make_unique<BattleInfo>(gameState().get());
	ASSERT_EQ(packet.info->getMagicRules().Struct().count("creatureAbilities"), 0u);
	CStackBasicDescriptor descriptor(CreatureID(0), 3);
	auto recipient = std::make_unique<CStack>(&descriptor, PlayerColor(0), 70, BattleSide::ATTACKER);
	recipient->restoreFrozenApplicationRound(2);
	ASSERT_FALSE(recipient->isNewHorizonsFrozen());
	packet.info->stacks.push_back(std::move(recipient));
	rejectsOlderWriterBeforePrefix(packet);
}

class NewHorizonsFrozenFoundationTest : public testing::Test
{
protected:
	UnitInfoMock info;
	UnitEnvironmentMock environment;
	BonusBearerMock bonuses;
	std::shared_ptr<battle::CUnitStateDetached> unit;
	void SetUp() override
	{
		using namespace testing;
		EXPECT_CALL(info, unitBaseAmount()).WillRepeatedly(Return(3));
		EXPECT_CALL(info, unitId()).WillRepeatedly(Return(7));
		EXPECT_CALL(info, unitSide()).WillRepeatedly(Return(BattleSide::ATTACKER));
		EXPECT_CALL(info, unitOwner()).WillRepeatedly(Return(PlayerColor(0)));
		EXPECT_CALL(info, unitSlot()).WillRepeatedly(Return(SlotID(0)));
		EXPECT_CALL(info, unitType()).WillRepeatedly(Return(CreatureID(0).toCreature()));
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, 10, BonusSourceID(CreatureID(0))));
		unit = std::make_shared<battle::CUnitStateDetached>(&info, &bonuses);
		unit->localInit(&environment);
		unit->setPosition(BattleHex(40));
	}
	Bonus frozen(int round = 2) const
	{
		return newHorizonsFrozen::marker(BonusSourceID(CreatureID(0)), round);
	}
	JsonNode configuredRules() const
	{
		return JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
	}
	void applyFrozen()
	{
		const auto marker = frozen();
		const auto prepared = newHorizonsFrozen::prepareApplication(*unit, {marker});
		bonuses.addNewBonus(std::make_shared<Bonus>(marker));
		unit->commitPreparedFrozenApplication(*prepared);
	}
};

TEST_F(NewHorizonsFrozenFoundationTest, ReceiptAndMalformedReceiptRejectBeforeUnitPacketPrefix)
{
	UnitChanges changes(unit->unitId(), BattleChanges::EOperation::UPDATE);
	changes.data = unit->save();
	for(const int receipt : {0, 2})
	{
		changes.data["state"]["frozenAppliedRound"].Integer() = receipt;
		rejectsOlderWriterBeforePrefix(changes);
		BattleUnitsChanged packet;
		packet.battleID = BattleID(0);
		packet.changedStacks = {changes};
		rejectsOlderWriterBeforePrefix(packet);
		BattleStackAttacked hit;
		hit.newState = changes;
		rejectsOlderWriterBeforePrefix(hit);
		BattleAttack attack;
		attack.battleID = BattleID(0);
		attack.attackerChanges = packet;
		rejectsOlderWriterBeforePrefix(attack);
		attack.attackerChanges.changedStacks.clear();
		attack.bsa = {hit};
		rejectsOlderWriterBeforePrefix(attack);
		StacksInjured injury;
		injury.battleID = BattleID(0);
		injury.stacks = {hit};
		rejectsOlderWriterBeforePrefix(injury);
	}
	for(const auto & malformed : {JsonNode(), JsonNode("2"), JsonNode(-2),
		JsonNode(static_cast<int64_t>(std::numeric_limits<int32_t>::max()) + 1)})
	{
		changes.data["state"]["frozenAppliedRound"] = malformed;
		CMemorySerializer writer;
		EXPECT_THROW(changes.serialize(writer.oser), std::runtime_error);
		EXPECT_TRUE(writer.extractBuffer().empty());
	}
}

TEST_F(NewHorizonsFrozenFoundationTest, AbsentAndDefaultReceiptsRemainOlderCompatible)
{
	UnitChanges changes(unit->unitId(), BattleChanges::EOperation::UPDATE);
	changes.data = unit->save();
	for(const bool absent : {false, true})
	{
		if(absent)
			changes.data["state"].Struct().erase("frozenAppliedRound");
		else
			changes.data["state"]["frozenAppliedRound"].Integer() = -1;
		CMemorySerializer memory;
		memory.oser.version = memory.iser.version = preFrozenVersion;
		ASSERT_NO_THROW(changes.serialize(memory.oser));
		UnitChanges decoded;
		ASSERT_NO_THROW(decoded.serialize(memory.iser));
		EXPECT_EQ(decoded.data, changes.data);
		EXPECT_EQ(decoded.id, changes.id);
	}
}

TEST_F(NewHorizonsFrozenFoundationTest, ExactFrozenBonusAndAllEffectOperationsRejectBeforePrefix)
{
	auto marker = frozen();
	rejectsOlderWriterBeforePrefix(marker);
	for(int operation = 0; operation < 3; ++operation)
	{
		SetStackEffect packet;
		packet.battleID = BattleID(0);
		auto & entries = operation == 0 ? packet.toAdd : operation == 1 ? packet.toUpdate : packet.toRemove;
		entries.emplace_back(unit->unitId(), std::vector<Bonus>{marker});
		rejectsOlderWriterBeforePrefix(packet);
	}
	CMemorySerializer current;
	Bonus decoded;
	ASSERT_NO_THROW(marker.serialize(current.oser));
	ASSERT_NO_THROW(decoded.serialize(current.iser));
	EXPECT_EQ(newHorizonsFrozen::markerApplicationRound(decoded), 2);
	EXPECT_EQ(decoded.statusIdentity, marker.statusIdentity);
}

TEST_F(NewHorizonsFrozenFoundationTest, OrdinaryCreatureBonusRemainsOlderCompatible)
{
	Bonus ordinary(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::CREATURE_ABILITY, -1, BonusSourceID(CreatureID(0)));
	SetStackEffect packet;
	packet.battleID = BattleID(0);
	packet.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{ordinary});
	Bonus poison(BonusDuration::ONE_BATTLE, BonusType::PHYSICAL_AFFLICTION,
		BonusSource::CREATURE_ABILITY, 0, BonusSourceID(CreatureID(0)));
	JsonNode metadata;
	metadata["kind"].String() = "poison";
	metadata["applicationOrder"].Integer() = 0;
	poison.parameters = std::make_shared<BonusParameters>(metadata);
	packet.toAdd.front().second.push_back(poison);
	CMemorySerializer memory;
	memory.oser.version = memory.iser.version = preFrozenVersion;
	ASSERT_NO_THROW(packet.serialize(memory.oser));
	SetStackEffect decoded;
	ASSERT_NO_THROW(decoded.serialize(memory.iser));
	ASSERT_EQ(decoded.toAdd.size(), 1u);
	ASSERT_EQ(decoded.toAdd.front().second.size(), 2u);
	EXPECT_EQ(decoded.toAdd.front().second.front().type, ordinary.type);
	EXPECT_EQ(decoded.toAdd.front().second.front().val, ordinary.val);
	const auto restoredPoison = physicalAfflictions::markerMetadata(decoded.toAdd.front().second.back());
	ASSERT_TRUE(restoredPoison);
	EXPECT_EQ(restoredPoison->kind, "poison");
}

TEST_F(NewHorizonsFrozenFoundationTest, PhysicalCureActionRequestRejectsBeforeServerPrefixWhileOrdinaryActionRoundTrips)
{
	MakeAction request;
	request.battleID = BattleID(0);
	request.ba.actionType = EActionType::HERO_SPELL;
	request.ba.side = BattleSide::ATTACKER;
	request.ba.spell = SpellID::CURE;
	request.ba.spellCurePhysicalAffliction = "frozen";
	rejectsOlderWriterBeforePrefix(request);
	request.ba.spellCurePhysicalAffliction.clear();
	CMemorySerializer memory;
	memory.oser.version = memory.iser.version = preFrozenVersion;
	ASSERT_NO_THROW(request.serialize(memory.oser));
	MakeAction decoded;
	ASSERT_NO_THROW(decoded.serialize(memory.iser));
	EXPECT_EQ(decoded.battleID, request.battleID);
	EXPECT_EQ(decoded.ba.actionType, request.ba.actionType);
	EXPECT_EQ(decoded.ba.spell, request.ba.spell);
	EXPECT_TRUE(decoded.ba.spellCurePhysicalAffliction.empty());
}

TEST_F(NewHorizonsFrozenFoundationTest, ShatterRejectsBeforeDirectAndOuterAttackInjuryPrefixes)
{
	BattleStackAttacked hit;
	hit.flags = BattleStackAttacked::SHATTER;
	rejectsOlderWriterBeforePrefix(hit);
	BattleAttack attack;
	attack.battleID = BattleID(0);
	attack.bsa = {hit};
	rejectsOlderWriterBeforePrefix(attack);
	StacksInjured injury;
	injury.battleID = BattleID(0);
	injury.stacks = {hit};
	rejectsOlderWriterBeforePrefix(injury);
	CMemorySerializer memory;
	ASSERT_NO_THROW(injury.serialize(memory.oser));
	StacksInjured decoded;
	ASSERT_NO_THROW(decoded.serialize(memory.iser));
	ASSERT_EQ(decoded.stacks.size(), 1u);
	EXPECT_TRUE(decoded.stacks.front().shattered());
}

TEST_F(NewHorizonsFrozenFoundationTest, DistinctPhysicalDebuffBlocksActionsWithoutStoneGazeAlias)
{
	applyFrozen();
	EXPECT_TRUE(unit->isNewHorizonsFrozen());
	EXPECT_FALSE(unit->isFrozen());
	EXPECT_FALSE(unit->canMove());
	EXPECT_FALSE(unit->canCast());
	EXPECT_FALSE(unit->canShoot());
	EXPECT_FALSE(unit->ableToRetaliate());
	const auto afflictions = physicalAfflictions::enumerate(*unit);
	ASSERT_EQ(afflictions.size(), 1);
	EXPECT_EQ(afflictions.front().kind, "frozen");
	EXPECT_TRUE(afflictions.front().effects.empty());
	// The marker shares the creature-source identity of native STACK_HEALTH.
	// Generic physical cleansing must still remove only Frozen.
	const auto physicalRemoval = physicalAfflictions::removalPlan(*unit, afflictions.front());
	ASSERT_EQ(physicalRemoval.size(), 1);
	EXPECT_EQ(physicalRemoval.front().type, BonusType::PHYSICAL_AFFLICTION);
	EXPECT_EQ(newHorizonsFrozen::removalPlan(*unit).size(), 1);
}

TEST_F(NewHorizonsFrozenFoundationTest, OnlyNormalQueueSlotConsumesFrozen)
{
	applyFrozen();
	EXPECT_TRUE(newHorizonsFrozen::forfeitsNormalActivation(*unit, BattleUnitTurnReason::TURN_QUEUE));
	for(const auto reason : {BattleUnitTurnReason::MORALE, BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION,
		BattleUnitTurnReason::HERO_COMMAND, BattleUnitTurnReason::AUTOMATIC_ACTION,
		BattleUnitTurnReason::ACTION_REJECTED})
		EXPECT_FALSE(newHorizonsFrozen::forfeitsNormalActivation(*unit, reason));
	EXPECT_EQ(unit->frozenLastAppliedRound(), 2);
}

TEST_F(NewHorizonsFrozenFoundationTest, CleansedStampPersistsThroughRoundHooksAndCopy)
{
	// The clean view models removal of the marker, without deleting local state.
	unit->recordFrozenApplication(2);
	unit->afterNewRound();
	EXPECT_FALSE(unit->isNewHorizonsFrozen());
	EXPECT_FALSE(newHorizonsFrozen::canApply(*unit, 2));
	EXPECT_FALSE(newHorizonsFrozen::canApply(*unit, 1));
	EXPECT_TRUE(newHorizonsFrozen::canApply(*unit, 3));
	const auto copy = unit->acquireState();
	EXPECT_EQ(copy->frozenLastAppliedRound(), 2);
	EXPECT_EQ(copy->getAvailableHealth(), unit->getAvailableHealth());
}

TEST_F(NewHorizonsFrozenFoundationTest, NoRefreshAndDuplicateBatchRejectedBeforeStampMutation)
{
	const auto value = frozen();
	EXPECT_THROW(newHorizonsFrozen::prepareApplication(*unit, {value, value}), std::invalid_argument);
	EXPECT_EQ(unit->frozenLastAppliedRound(), -1);
	applyFrozen();
	EXPECT_THROW(newHorizonsFrozen::prepareApplication(*unit, {frozen(3)}), std::invalid_argument);
	EXPECT_EQ(unit->frozenLastAppliedRound(), 2);
}

TEST_F(NewHorizonsFrozenFoundationTest, StampJsonRoundTripPreservesRecipientHealth)
{
	unit->recordFrozenApplication(2);
	const auto beforeHealth = unit->getAvailableHealth();
	const auto saved = unit->save();
	const auto restored = unit->acquireState();
	restored->load(saved);
	EXPECT_EQ(restored->frozenLastAppliedRound(), 2);
	EXPECT_EQ(restored->getAvailableHealth(), beforeHealth);
	EXPECT_EQ(restored->getKilled(), unit->getKilled());
}

TEST_F(NewHorizonsFrozenFoundationTest, MalformedRoundRejectedWithoutChangingExistingState)
{
	unit->recordFrozenApplication(2);
	const auto beforeHealth = unit->getAvailableHealth();
	for(const auto raw : {-2LL, 2147483648LL})
	{
		auto saved = unit->save();
		saved["state"]["frozenAppliedRound"].Integer() = raw;
		EXPECT_THROW(unit->load(saved), std::runtime_error);
		EXPECT_EQ(unit->frozenLastAppliedRound(), 2);
		EXPECT_EQ(unit->getAvailableHealth(), beforeHealth);
	}
	auto saved = unit->save();
	saved["state"]["frozenAppliedRound"].String() = "2";
	EXPECT_THROW(unit->load(saved), std::runtime_error);
	EXPECT_EQ(unit->frozenLastAppliedRound(), 2);
}

TEST_F(NewHorizonsFrozenFoundationTest, ShippedCapturedConfigurationAndOmittedLegacyControls)
{
	auto rules = configuredRules();
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_EQ(newHorizonsFrozen::chancePercent(rules), 20);
	EXPECT_EQ(newHorizonsFrozen::shatterBonusPercent(rules), 25);
	rules.Struct().erase("creatureAbilities");
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_EQ(newHorizonsFrozen::chancePercent(rules), 0);
	EXPECT_EQ(newHorizonsFrozen::shatterBonusPercent(rules), 0);
	EXPECT_EQ(newHorizonsFrozen::chancePercent(JsonNode()), 0);
	EXPECT_EQ(newHorizonsFrozen::shatterBonusPercent(JsonNode()), 0);
}

TEST_F(NewHorizonsFrozenFoundationTest, ConfigurationRejectsMalformedTypesRangesAndUnknownFields)
{
	for(const auto * key : {"freezingTouchChancePercent", "shatterBonusPercent"})
	{
		for(const auto value : {-1, 101})
		{
			auto rules = configuredRules();
			rules["creatureAbilities"][key].Integer() = value;
			EXPECT_ANY_THROW(newHorizonsMagic::validateRules(rules));
			EXPECT_EQ(newHorizonsFrozen::chancePercent(rules), 0);
		}
		auto rules = configuredRules();
		rules["creatureAbilities"][key].Float() = 20.5;
		EXPECT_ANY_THROW(newHorizonsMagic::validateRules(rules));
		rules = configuredRules();
		rules["creatureAbilities"].Struct().erase(key);
		EXPECT_ANY_THROW(newHorizonsMagic::validateRules(rules));
	}
	auto rules = configuredRules();
	rules["creatureAbilities"]["unexpected"].Integer() = 1;
	EXPECT_ANY_THROW(newHorizonsMagic::validateRules(rules));
	rules = configuredRules();
	rules["creatureAbilities"]["rulesetVersion"].Integer() = 2;
	EXPECT_ANY_THROW(newHorizonsMagic::validateRules(rules));
}

TEST_F(NewHorizonsFrozenFoundationTest, ConfigurableZeroAndMaximumValuesAreCapturedNotGlobal)
{
	auto rules = configuredRules();
	rules["creatureAbilities"]["freezingTouchChancePercent"].Integer() = 0;
	rules["creatureAbilities"]["shatterBonusPercent"].Integer() = 100;
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_EQ(newHorizonsFrozen::chancePercent(rules), 0);
	EXPECT_EQ(newHorizonsFrozen::shatterBonusPercent(rules), 100);
	EXPECT_FALSE(newHorizonsFrozen::isFreezingTouchAttacker(*unit, rules));
}

TEST_F(NewHorizonsFrozenFoundationTest, ShatterUsesPhysicalAttackProvenanceIncludingCollateralAndRetaliation)
{
	applyFrozen();
	BonusBearerMock cleanBonuses;
	cleanBonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, 10, BonusSourceID(CreatureID(0))));
	const auto attacker = std::make_shared<battle::CUnitStateDetached>(&info, &cleanBonuses);
	attacker->localInit(&environment);
	BattleAttackInfo attack(attacker.get(), unit.get(), 0, false);
	const auto rules = configuredRules();
	EXPECT_TRUE(newHorizonsFrozen::qualifiesForShatter(attack, rules));
	attack.secondaryAttack = true;
	attack.retaliation = true;
	EXPECT_TRUE(newHorizonsFrozen::qualifiesForShatter(attack, rules));
	attack.physicalDamage = false;
	EXPECT_FALSE(newHorizonsFrozen::qualifiesForShatter(attack, rules));
	attack.physicalDamage = true;
	EXPECT_FALSE(newHorizonsFrozen::qualifiesForShatter(attack, JsonNode()));
	attack.defender = nullptr;
	EXPECT_FALSE(newHorizonsFrozen::qualifiesForShatter(attack, rules));
}

TEST_F(NewHorizonsFrozenFoundationTest, FreezingTouchRequiresExactIceBodyAndUnblockedOrdinaryAttacker)
{
	using namespace testing;
	const auto rules = configuredRules();
	EXPECT_FALSE(newHorizonsFrozen::isFreezingTouchAttacker(*unit, rules));
	const auto * ice = CreatureID(CreatureID::decode("core:iceElemental")).toCreature();
	ASSERT_NE(ice, nullptr);
	EXPECT_CALL(info, unitType()).WillRepeatedly(Return(ice));
	EXPECT_TRUE(newHorizonsFrozen::isFreezingTouchAttacker(*unit, rules));
	EXPECT_FALSE(newHorizonsFrozen::isFreezingTouchAttacker(*unit, JsonNode()));
	EXPECT_CALL(info, unitSlot()).WillRepeatedly(Return(SlotID::WAR_MACHINES_SLOT));
	EXPECT_FALSE(newHorizonsFrozen::isFreezingTouchAttacker(*unit, rules));
	EXPECT_CALL(info, unitSlot()).WillRepeatedly(Return(SlotID(0)));
	applyFrozen();
	EXPECT_FALSE(newHorizonsFrozen::isFreezingTouchAttacker(*unit, rules));
}
}
