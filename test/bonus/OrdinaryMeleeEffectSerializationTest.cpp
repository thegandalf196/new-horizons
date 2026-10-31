/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/battle/NewHorizonsRangefinder.h"
#include "../../lib/battle/NewHorizonsMaintenanceSpecialties.h"
#include "../../lib/battle/NewHorizonsShieldAndSword.h"
#include "../../lib/battle/NewHorizonsSepulchralGrip.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/gameState/ReplayLog.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/networkPacks/SetRewardableConfiguration.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/GameSettings.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include <array>
#include <algorithm>
#include <tuple>

namespace
{
using Version = ESerializationVersion;
constexpr std::array<Version, 7> oldEpochs{{static_cast<Version>(1144), static_cast<Version>(1145),
	static_cast<Version>(1146), static_cast<Version>(1147), static_cast<Version>(1148), static_cast<Version>(1149), static_cast<Version>(1150)}};
Bonus ordinary()
{
	return {BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 7, BonusSourceID()};
}
bool isMarker(const Bonus & bonus)
{
	return newHorizonsShieldAndSword::isProtection(&bonus) || newHorizonsSepulchralGrip::isEffect(&bonus);
}
Bonus makeMarker(int value)
{
	const bool grip = value < 0;
	Bonus result(grip ? BonusDuration::STACK_ACTIVATION : BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION,
		grip ? BonusType::STACKS_MOVEMENT_RANGE : BonusType::GENERAL_DAMAGE_REDUCTION,
		BonusSource::HERO_SPECIAL, value, BonusSourceID(HeroTypeID(HeroTypeID::decode(grip ? "core:straker" : "core:sorsha"))),
		grip ? BonusSubtypeID() : BonusSubtypeID(BonusCustomSubtype::damageTypeAll),
		grip ? BonusValueType::ADDITIVE_VALUE : BonusValueType::BASE_NUMBER);
	result.stacking = std::string("new-horizons:ordinary-creature-specialty:")
		+ (grip ? "core:straker:sepulchralGrip" : "core:sorsha:shieldAndSword");
	result.statusIdentity = grip ? "sepulchralGrip" : "shieldAndSword";
	result.statusTags = grip ? std::vector{BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE}
		: std::vector{BonusStatusTag::NON_TRANSFERABLE};
	return result;
}

std::shared_ptr<BonusParameters> effects(std::shared_ptr<const Bonus> child)
{
	BonusParametersOnCombatEvent event;
	event.effects.push_back(BonusParametersOnCombatEvent::CombatEffectBonus{child, false});
	return std::make_shared<BonusParameters>(event);
}
class OrdinaryMeleeEffectSerializationTest : public BattleTestFixture
{
protected:
	const char * expectedUnsupported = "Unsupported Shield and Sword bonus format";
	const char * expectedMalformed = "Invalid Shield and Sword specialty provenance";
	Bonus marker(int value)
	{
		expectedUnsupported = value < 0 ? "Unsupported Sepulchral Grip bonus format" : "Unsupported Shield and Sword bonus format";
		expectedMalformed = value < 0 ? "Invalid Sepulchral Grip specialty provenance" : "Invalid Shield and Sword specialty provenance";
		return makeMarker(value);
	}
	template<class Decode> void expectMarkerRefusal(Decode decode)
	{
		try { decode(); FAIL() << "Expected exact reserved melee capability rejection"; }
		catch(const std::runtime_error & error) { EXPECT_EQ(std::string(error.what()), expectedUnsupported); }
	}
	template<class T> void oldPrefixRejected(T & source)
	{
		for(const auto epoch : oldEpochs)
		{
			CMemorySerializer wire;
			wire.oser.version = epoch;
			expectMarkerRefusal([&] { wire.oser & source; });
			EXPECT_TRUE(wire.extractBuffer().empty());
		}
	}
	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		// Isolate the actual owned bonus guard from captured specialty admission.
		rules["additionalSpecialties"].Struct().erase("ordinaryCreatureSpecialties");
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	template<class T> void currentRoundtrip(T & source)
	{
		CMemorySerializer wire;
		wire.iser.cb = gameState().get();
		ASSERT_NO_THROW(wire.oser & source);
		T restored;
		ASSERT_NO_THROW(wire.iser & restored);
	}
};
TEST_F(OrdinaryMeleeEffectSerializationTest, DirectBonusRetainsBothValuesAndRefusesOldWritersAndReaders)
{
	startGame();
	for(const int captured : {20, -1, 100, -100})
	{
		auto source = marker(captured);
		ASSERT_TRUE(isMarker(source));
		CMemorySerializer wire;
		ASSERT_NO_THROW(wire.oser & source);
		Bonus restored;
		ASSERT_NO_THROW(wire.iser & restored);
		ASSERT_TRUE(isMarker(restored));
		EXPECT_EQ(restored.val, captured);
		oldPrefixRejected(source);
		const auto bytes = wire.extractBuffer();
		for(const auto epoch : oldEpochs)
		{
			CMemorySerializer historical(bytes);
			historical.iser.version = epoch;
			Bonus decoded; // Discard failed decoded values; there is no rollback contract.
			expectMarkerRefusal([&] { historical.iser & decoded; });
		}
	}
}
TEST_F(OrdinaryMeleeEffectSerializationTest, LegacyPdrAndMovementBytesRemainIdenticalAndReservedMalformedProvenanceFailsClosed)
{
	startGame();
	for(const int value : {20, -1})
	{
		auto source = marker(value);
		source.stacking = "unrelated-legacy-physical-status";
		for(const auto epoch : oldEpochs)
		{
			CMemorySerializer old, current;
			old.oser.version = old.iser.version = epoch;
			ASSERT_NO_THROW(old.oser & source);
			Bonus decoded;
			ASSERT_NO_THROW(old.iser & decoded);
			ASSERT_NO_THROW(current.oser & source);
			EXPECT_EQ(old.extractBuffer(), current.extractBuffer());
		}
		for(int bad = 0; bad < 5; ++bad)
		{
			auto malformed = marker(value);
			if(bad == 0) malformed.source = BonusSource::OTHER;
			if(bad == 1) malformed.limiter = std::make_shared<CCreatureTypeLimiter>();
			if(bad == 2) malformed.effectRange = BonusLimitEffect::ONLY_DISTANCE_FIGHT;
			if(bad == 3) malformed.val = 0; // Zero policy produces no effect, not a reserved zero marker.
			if(bad == 4) malformed.turnsRemain = 1;
			for(const auto epoch : {static_cast<Version>(1150), Version::CURRENT})
			{
				CMemorySerializer wire;
				wire.oser.version = epoch;
				try { wire.oser & malformed; FAIL() << "Expected exact malformed marker rejection"; }
				catch(const std::runtime_error & error) { EXPECT_EQ(std::string(error.what()), expectedMalformed); }
				EXPECT_TRUE(wire.extractBuffer().empty());
			}
		}
	}
}
TEST_F(OrdinaryMeleeEffectSerializationTest, DecodedMalformedReservedIdentityFailsAtDirectBonusGuard)
{
	startGame();
	for(const int value : {20, -1})
	{
	auto source = marker(value);
	source.source = BonusSource::OTHER;
	source.stacking.front() = 'X'; // A valid unrelated legacy bonus until the wire identity is changed.
	CMemorySerializer wire;
	ASSERT_NO_THROW(wire.oser & source);
	const auto ordinaryBytes = wire.extractBuffer();
	CMemorySerializer valid(ordinaryBytes);
	Bonus unrelated;
	ASSERT_NO_THROW(valid.iser & unrelated);
	EXPECT_FALSE(isMarker(unrelated));
	// Construct malformed input without reimplementing Bonus's layout: change only
	// the unique equal-length stacking text in a real ordinary codec output.
	auto malformed = ordinaryBytes;
	std::vector<std::byte> text;
	for(const unsigned char ch : source.stacking) text.push_back(static_cast<std::byte>(ch));
	const auto at = std::search(malformed.begin(), malformed.end(), text.begin(), text.end());
	ASSERT_NE(at, malformed.end());
	ASSERT_EQ(std::search(at + text.size(), malformed.end(), text.begin(), text.end()), malformed.end());
	*at = static_cast<std::byte>('n');
	for(const auto epoch : {static_cast<Version>(1150), Version::CURRENT})
	{
		CMemorySerializer bad(malformed);
		bad.iser.version = epoch;
		Bonus decoded;
		try { bad.iser & decoded; FAIL() << "Expected malformed reserved provenance rejection"; }
		catch(const std::runtime_error & error)
		{
			EXPECT_EQ(std::string(error.what()), expectedMalformed);
		}
	}
	}
}
TEST_F(OrdinaryMeleeEffectSerializationTest, NestedTypedPayloadListsAndNodesRejectBeforeTheirPrefixes)
{
	startGame();
	for(const int captured : {20, -1, 100, -100})
	{
		auto child = std::make_shared<Bonus>(marker(captured));
		auto parent = std::make_shared<Bonus>(ordinary());
		auto parameters = effects(child);
		parent->parameters = parameters;
		BonusList list;
		list.push_back(parent);
		CBonusSystemNode node(BonusNodeType::ARMY);
		node.addNewBonus(parent);
		oldPrefixRejected(*parent);
		oldPrefixRejected(*parameters);
		oldPrefixRejected(list);
		oldPrefixRejected(node);
		currentRoundtrip(*parent);
		currentRoundtrip(*parameters);
		currentRoundtrip(list);
		CBonusSystemNode restoredNode(BonusNodeType::ARMY);
		CMemorySerializer nodeWire;
		nodeWire.iser.cb = gameState().get();
		ASSERT_NO_THROW(nodeWire.oser & node);
		ASSERT_NO_THROW(nodeWire.iser & restoredNode);
		expectMarkerRefusal([&] { restoredNode.validateExportedBonusSerialization(BonusSerializationContext{true, true, true, true, false}); });
	}
}
TEST_F(OrdinaryMeleeEffectSerializationTest, ChangeListsCommanderAndRewardConfigurationProtectOuterPrefixes)
{
	startGame();
	for(const int captured : {20, -1, 100, -100})
	{
		for(size_t i = 0; i < 3; ++i)
		{
			SetStackEffect packet;
			packet.battleID = BattleID(0);
			const std::array lists{&packet.toAdd, &packet.toUpdate, &packet.toRemove};
			lists[i]->push_back({1, {marker(captured)}});
			oldPrefixRejected(packet);
			currentRoundtrip(packet);
		}
		SetCommanderProperty commander;
		commander.accumulatedBonus = marker(captured);
		oldPrefixRejected(commander);
		currentRoundtrip(commander);
		SetRewardableConfiguration reward;
		reward.configuration.info.emplace_back();
		reward.configuration.info.back().reward.commanderBonuses.push_back(std::make_shared<Bonus>(marker(captured)));
		oldPrefixRejected(reward);
		currentRoundtrip(reward);
	}
}
TEST_F(OrdinaryMeleeEffectSerializationTest, ReplayUsesDeclaredCodecAndRefusesMislabeledHistoricalBonus)
{
	startGame();
	for(const int captured : {20, -1, 100, -100})
	{
		SetStackEffect packet;
		packet.battleID = BattleID(0);
		packet.toAdd.push_back({1, {marker(captured)}});
		auto payload = ReplayPackSerializer::write(packet);
		EXPECT_EQ(payload.version, Version::CURRENT);
		auto decoded = ReplayPackSerializer::read(payload, gameState().get());
		const auto * changes = dynamic_cast<SetStackEffect *>(decoded.get());
		ASSERT_NE(changes, nullptr);
		ASSERT_EQ(changes->toAdd.size(), 1u);
		ASSERT_FALSE(changes->toAdd.front().second.empty());
		ASSERT_TRUE(isMarker(changes->toAdd.front().second.front()));
		for(const auto epoch : oldEpochs)
		{
			payload.version = epoch;
			expectMarkerRefusal([&] { ReplayPackSerializer::read(payload, gameState().get()); });
		}
	}
}

TEST_F(OrdinaryMeleeEffectSerializationTest, NestedExchangeSnapshotsRetainSpellEffectShapeAndRejectBeforePrefixes)
{
	startGame();
	for(size_t endpoint = 0; endpoint < 2; ++endpoint)
		for(const bool replacement : {false, true})
		{
			battle::BattleEffectExchange exchange;
			for(size_t i = 0; i < exchange.endpoints.size(); ++i)
			{
				exchange.endpoints[i].id = static_cast<uint32_t>(i + 1);
				exchange.endpoints[i].expected.recipientHealth.Struct();
				exchange.endpoints[i].replacement.recipientHealth.Struct();
			}
			auto parent = ordinary();
			parent.source = BonusSource::SPELL_EFFECT;
			parent.parameters = effects(std::make_shared<Bonus>(marker(1)));
			auto & selected = exchange.endpoints[endpoint];
			auto & snapshot = replacement ? selected.replacement : selected.expected;
			snapshot.effects.push_back(parent);
			ASSERT_NO_THROW(exchange.validateShape());
			oldPrefixRejected(snapshot);
			oldPrefixRejected(selected);
			oldPrefixRejected(exchange);
			SetStackEffect packet;
			packet.battleID = BattleID(0);
			packet.exchange = exchange;
			oldPrefixRejected(packet);
			currentRoundtrip(packet);
		}
}
TEST_F(OrdinaryMeleeEffectSerializationTest, PreTriggerOwnershipAndTypedMdrCapabilityRemainDistinct)
{
	startGame();
	auto source = ordinary();
	source.parameters = effects(std::make_shared<Bonus>(marker(1)));
	const auto retained = source.parameters;
	for(const int epoch : {893, 899, 900})
	{
		CMemorySerializer old;
		old.oser.version = old.iser.version = static_cast<Version>(epoch);
		const auto context = BonusSerializationContext::fromHandler(old.oser);
		EXPECT_FALSE(context.combatEventParametersSerialized);
		ASSERT_NO_THROW(old.oser & source); // Parameters are not in this target's Bonus wire.
		EXPECT_EQ(source.parameters, retained);
		Bonus restored = ordinary();
		restored.parameters = retained;
		ASSERT_NO_THROW(old.iser & restored);
		EXPECT_EQ(restored.parameters, retained); // Empty oldAddInfo does not reset reused state.
	}
	CMemorySerializer trigger;
	trigger.oser.version = Version::BONUS_TRIGGER;
	EXPECT_TRUE(BonusSerializationContext::fromHandler(trigger.oser).combatEventParametersSerialized);
	expectMarkerRefusal([&] { trigger.oser & source; });
	EXPECT_TRUE(trigger.extractBuffer().empty());
	auto mdr = ordinary();
	mdr.type = BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION;
	for(const int epoch : {1145, 1146, 1147, 1148, 1149, 1150})
	{
		CMemorySerializer admitted;
		admitted.oser.version = admitted.iser.version = static_cast<Version>(epoch);
		ASSERT_NO_THROW(admitted.oser & mdr);
		Bonus restored;
		ASSERT_NO_THROW(admitted.iser & restored);
		EXPECT_EQ(restored.type, mdr.type);
	}
}

TEST_F(OrdinaryMeleeEffectSerializationTest, OwnedAliasesAndCycleTerminateBeforePendingMarker)
{
	startGame();
	auto root = std::make_shared<Bonus>(ordinary());
	auto child = std::make_shared<Bonus>(ordinary());
	auto leaf = std::make_shared<Bonus>(marker(1));
	root->parameters = effects(child);
	BonusParametersOnCombatEvent event;
	event.effects.push_back(BonusParametersOnCombatEvent::CombatEffectBonus{leaf, false});
	event.effects.push_back(BonusParametersOnCombatEvent::CombatEffectBonus{root, false});
	event.effects.push_back(BonusParametersOnCombatEvent::CombatEffectBonus{root, true});
	child->parameters = std::make_shared<BonusParameters>(event);
	oldPrefixRejected(*root); // Repeated root must be skipped before the pending marker is reached.
	EXPECT_NO_THROW(root->validateAdjacencyCapabilitySerialization(BonusSerializationContext{true, true, true, true, true}));
	root->parameters.reset();
	child->parameters.reset(); // Break owned source cycle even when nonfatal expectations fail.
}

TEST_F(OrdinaryMeleeEffectSerializationTest, PriorRangefinderAndOgreBulwarkRetainTheirSeparateCapabilities)
{
	startGame();
	Bonus range(BonusDuration::PERMANENT, BonusType::LIMITED_SHOOTING_RANGE, BonusSource::HERO_SPECIAL,
		255, BonusSourceID(HeroTypeID(HeroTypeID::decode("core:christian"))), BonusSubtypeID(), BonusValueType::BASE_NUMBER);
	range.stacking = "new-horizons:ordinary-creature-specialty:core:christian";
	auto ballista = std::make_shared<CCreatureTypeLimiter>();
	ballista->creatureID = CreatureID(CreatureID::decode("core:ballista"));
	ballista->includeUpgrades = true;
	range.limiter = ballista;
	newHorizonsRangefinder::configureEnvelope(range, 12);
	Bonus ogre(BonusDuration::PERMANENT, BonusType::ADDITIONAL_RETALIATION, BonusSource::HERO_SPECIAL,
		0, BonusSourceID(HeroTypeID(HeroTypeID::decode("core:krellion"))), BonusSubtypeID(), BonusValueType::BASE_NUMBER);
	ogre.stacking = "new-horizons:ordinary-creature-specialty:core:krellion";
	auto family = std::make_shared<CCreatureTypeLimiter>();
	family->creatureID = CreatureID(CreatureID::decode("core:ogre"));
	family->includeUpgrades = true;
	ogre.limiter = family;
	for(auto * source : {&range, &ogre})
	{
		CMemorySerializer wire;
		wire.oser.version = wire.iser.version = static_cast<Version>(source == &range ? 1148 : 1149);
		ASSERT_NO_THROW(wire.oser & *source);
		Bonus decoded;
		ASSERT_NO_THROW(wire.iser & decoded);
		EXPECT_EQ(decoded.toJsonNode(), source->toJsonNode());
		EXPECT_FALSE(BonusSerializationContext::fromHandler(wire.oser).ordinaryCombatSpecialtiesSupported);
	}
}

enum class OwnedLocation { HERO, SLOT, PLAYER, PLAYER_BATTLE, TEAM, GLOBAL, POOL, ARTIFACT };
class OrdinaryMeleeOwnedWorldSerializationTest : public OrdinaryMeleeEffectSerializationTest,
	public ::testing::WithParamInterface<std::tuple<OwnedLocation, bool>> {};
TEST_P(OrdinaryMeleeOwnedWorldSerializationTest, RealOwnedBonusProtectsMapWorldAndCurrentSaveRetainsIdentity)
{
	startGame(true);
	const auto location = std::get<0>(GetParam());
	const int value = std::get<1>(GetParam()) ? -1 : 20;
	CBonusSystemNode * target = nullptr;
	HeroTypeID pooled;
	switch(location)
	{
	case OwnedLocation::HERO: target = attackerSideHero; break;
	case OwnedLocation::SLOT: target = attackerSideHero->getStackPtr(SlotID(0)); break;
	case OwnedLocation::PLAYER: target = &gameState()->players.at(PlayerColor(0)); break;
	case OwnedLocation::PLAYER_BATTLE: break;
	case OwnedLocation::TEAM: target = &gameState()->teams.begin()->second; break;
	case OwnedLocation::GLOBAL: target = &gameState()->globalEffects; break;
	case OwnedLocation::POOL:
	{
		const auto pool = map()->getHeroesInPool();
		ASSERT_FALSE(pool.empty());
		pooled = pool.front();
		target = map()->tryGetFromHeroPool(pooled);
		break;
	}
	case OwnedLocation::ARTIFACT: target = gameState()->createArtifact(ArtifactID(0), SpellID::NONE); break;
	}
	if(location != OwnedLocation::PLAYER_BATTLE) ASSERT_NE(target, nullptr);
	// These real old writers must succeed before injection; no JSON policy rejection may mask the marker.
	for(const auto epoch : oldEpochs)
	{
		CMemorySerializer world, mapped;
		world.oser.version = mapped.oser.version = epoch;
		ASSERT_NO_THROW(world.oser & *gameState());
		ASSERT_NO_THROW(mapped.oser & *map());
	}
	// Both produced semantic identities need1151 even though their underlying bonus types are older.
	if(location == OwnedLocation::PLAYER_BATTLE)
		gameState()->players.at(PlayerColor(0)).battleBonuses.push_back(marker(value));
	else target->addNewBonus(std::make_shared<Bonus>(marker(value)));
	CGameState restored;
	restored.preInit(LIBRARY);
	ASSERT_NO_THROW(restored.loadFromMemory(gameState()->saveToMemory()));
	auto previous = BonusSerializationContext{true, true, true, true, false};
	expectMarkerRefusal([&] { restored.validateOwnedBonusSerialization(previous); });
	if(location == OwnedLocation::POOL)
	{
		const auto * hero = restored.getMap().tryGetFromHeroPool(pooled);
		ASSERT_NE(hero, nullptr);
		ASSERT_FALSE(hero->getExportedBonusList().empty());
		ASSERT_TRUE(isMarker(*hero->getExportedBonusList().back()));
	}
	oldPrefixRejected(*gameState());
	if(location == OwnedLocation::HERO || location == OwnedLocation::SLOT
		|| location == OwnedLocation::POOL || location == OwnedLocation::ARTIFACT)
		oldPrefixRejected(*map());
	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & *gameState());
	CMemorySerializer mislabeled(current.extractBuffer());
	mislabeled.iser.version = static_cast<Version>(1150);
	mislabeled.iser.loadingGamestate = true;
	CGameState failed; // Whole-world decode is not transactional; discard the fresh owner.
	failed.preInit(LIBRARY);
	mislabeled.iser.cb = &failed;
	expectMarkerRefusal([&] { mislabeled.iser & failed; });
}
INSTANTIATE_TEST_SUITE_P(OwnedPayloads, OrdinaryMeleeOwnedWorldSerializationTest, ::testing::Combine(::testing::Values(
	OwnedLocation::HERO, OwnedLocation::SLOT, OwnedLocation::PLAYER, OwnedLocation::PLAYER_BATTLE,
	OwnedLocation::TEAM, OwnedLocation::GLOBAL, OwnedLocation::POOL, OwnedLocation::ARTIFACT), ::testing::Bool()));
}
