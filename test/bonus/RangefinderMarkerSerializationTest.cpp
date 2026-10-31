/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/battle/NewHorizonsRangefinder.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/gameState/ReplayLog.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/networkPacks/SetRewardableConfiguration.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/GameSettings.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <array>
#include <algorithm>

namespace
{
using Version = ESerializationVersion;
constexpr std::array<Version, 4> oldEpochs{{static_cast<Version>(1144), static_cast<Version>(1145),
	static_cast<Version>(1146), static_cast<Version>(1147)}};
Bonus ordinary()
{
	return {BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 7, BonusSourceID()};
}
Bonus marker(int captured)
{
	Bonus result(BonusDuration::PERMANENT, BonusType::LIMITED_SHOOTING_RANGE, BonusSource::HERO_SPECIAL,
		255, BonusSourceID(HeroTypeID(HeroTypeID::decode("core:christian"))));
	result.stacking = "new-horizons:ordinary-creature-specialty:core:christian";
	result.valType = BonusValueType::BASE_NUMBER;
	auto family = std::make_shared<CCreatureTypeLimiter>();
	family->creatureID = CreatureID(CreatureID::decode("core:ballista"));
	family->includeUpgrades = true;
	result.limiter = family;
	newHorizonsRangefinder::configureEnvelope(result, captured);
	return result;
}
std::shared_ptr<BonusParameters> effects(std::shared_ptr<const Bonus> child)
{
	BonusParametersOnCombatEvent event;
	event.effects.push_back(BonusParametersOnCombatEvent::CombatEffectBonus{child, false});
	return std::make_shared<BonusParameters>(event);
}
template<class Decode> void expectMarkerRefusal(Decode decode)
{
	try { decode(); FAIL() << "Expected reserved Rangefinder format rejection"; }
	catch(const std::runtime_error & error)
	{
		EXPECT_EQ(std::string(error.what()), "Unsupported Rangefinder envelope bonus format");
	}
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
class RangefinderMarkerSerializationTest : public BattleTestFixture
{
protected:
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
TEST_F(RangefinderMarkerSerializationTest, DirectBonusRetainsBothDistancesAndRefusesOldWritersAndReaders)
{
	startGame();
	for(const int captured : {0, 12})
	{
		auto source = marker(captured);
		ASSERT_TRUE(newHorizonsRangefinder::isEnvelope(source));
		CMemorySerializer wire;
		ASSERT_NO_THROW(wire.oser & source);
		Bonus restored;
		ASSERT_NO_THROW(wire.iser & restored);
		ASSERT_TRUE(newHorizonsRangefinder::isEnvelope(restored));
		EXPECT_EQ(restored.parameters->toNumber(), captured == 0 ? 10 : 12);
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
TEST_F(RangefinderMarkerSerializationTest, LegacyLimitedBytesRemainIdenticalAndReservedMalformedProvenanceFailsClosed)
{
	startGame();
	auto source = marker(12);
	source.stacking = "unrelated-legacy-range";
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
	for(const int bad : {0, 1, 2})
	{
		auto malformed = marker(12);
		if(bad == 0) malformed.source = BonusSource::OTHER;
		if(bad == 1) malformed.limiter = std::make_shared<CCreatureTypeLimiter>();
		if(bad == 2) malformed.effectRange = BonusLimitEffect::ONLY_DISTANCE_FIGHT;
		for(const auto epoch : {static_cast<Version>(1147), Version::CURRENT})
		{
			CMemorySerializer wire;
			wire.oser.version = epoch;
			EXPECT_THROW(wire.oser & malformed, std::runtime_error);
			EXPECT_TRUE(wire.extractBuffer().empty());
		}
	}
}
TEST_F(RangefinderMarkerSerializationTest, DecodedMalformedReservedIdentityFailsAtDirectBonusGuard)
{
	startGame();
	auto source = marker(12);
	source.source = BonusSource::OTHER;
	source.stacking.front() = 'X'; // A valid unrelated legacy bonus until the wire identity is changed.
	CMemorySerializer wire;
	ASSERT_NO_THROW(wire.oser & source);
	const auto ordinaryBytes = wire.extractBuffer();
	CMemorySerializer valid(ordinaryBytes);
	Bonus unrelated;
	ASSERT_NO_THROW(valid.iser & unrelated);
	EXPECT_FALSE(newHorizonsRangefinder::isEnvelope(unrelated));
	// Construct malformed input without reimplementing Bonus's layout: change only
	// the unique equal-length stacking text in a real ordinary codec output.
	auto malformed = ordinaryBytes;
	std::vector<std::byte> text;
	for(const unsigned char ch : source.stacking) text.push_back(static_cast<std::byte>(ch));
	const auto at = std::search(malformed.begin(), malformed.end(), text.begin(), text.end());
	ASSERT_NE(at, malformed.end());
	ASSERT_EQ(std::search(at + text.size(), malformed.end(), text.begin(), text.end()), malformed.end());
	*at = static_cast<std::byte>('n');
	for(const auto epoch : {static_cast<Version>(1147), Version::CURRENT})
	{
		CMemorySerializer bad(malformed);
		bad.iser.version = epoch;
		Bonus decoded;
		try { bad.iser & decoded; FAIL() << "Expected malformed reserved provenance rejection"; }
		catch(const std::runtime_error & error)
		{
			EXPECT_EQ(std::string(error.what()), "Invalid Rangefinder envelope provenance");
		}
	}
}
TEST_F(RangefinderMarkerSerializationTest, NestedTypedPayloadListsAndNodesRejectBeforeTheirPrefixes)
{
	startGame();
	for(const int captured : {0, 12})
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
	}
}
TEST_F(RangefinderMarkerSerializationTest, ChangeListsCommanderAndRewardConfigurationProtectOuterPrefixes)
{
	startGame();
	for(const int captured : {0, 12})
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
TEST_F(RangefinderMarkerSerializationTest, ReplayUsesDeclaredCodecAndRefusesMislabeledHistoricalBonus)
{
	startGame();
	for(const int captured : {0, 12})
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
		ASSERT_TRUE(newHorizonsRangefinder::isEnvelope(changes->toAdd.front().second.front()));
		for(const auto epoch : oldEpochs)
		{
			payload.version = epoch;
			expectMarkerRefusal([&] { ReplayPackSerializer::read(payload, gameState().get()); });
		}
	}
}

TEST_F(RangefinderMarkerSerializationTest, NestedExchangeSnapshotsRetainSpellEffectShapeAndRejectBeforePrefixes)
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
			parent.parameters = effects(std::make_shared<Bonus>(marker(12)));
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
TEST_F(RangefinderMarkerSerializationTest, PreTriggerOwnershipAndTypedMdrCapabilityRemainDistinct)
{
	startGame();
	auto source = ordinary();
	source.parameters = effects(std::make_shared<Bonus>(marker(12)));
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
	for(const int epoch : {1145, 1146, 1147})
	{
		CMemorySerializer admitted;
		admitted.oser.version = admitted.iser.version = static_cast<Version>(epoch);
		ASSERT_NO_THROW(admitted.oser & mdr);
		Bonus restored;
		ASSERT_NO_THROW(admitted.iser & restored);
		EXPECT_EQ(restored.type, mdr.type);
	}
}

enum class OwnedLocation { HERO, SLOT, PLAYER, PLAYER_BATTLE, TEAM, GLOBAL, POOL, ARTIFACT };
class RangefinderOwnedWorldSerializationTest : public RangefinderMarkerSerializationTest,
	public ::testing::WithParamInterface<OwnedLocation> {};
TEST_P(RangefinderOwnedWorldSerializationTest, RealOwnedBonusProtectsMapWorldAndCurrentSaveRetainsIdentity)
{
	startGame(true);
	const auto location = GetParam();
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
	// The explicit-zero policy produces threshold10, but keeps the new exclusion identity.
	if(location == OwnedLocation::PLAYER_BATTLE)
		gameState()->players.at(PlayerColor(0)).battleBonuses.push_back(marker(0));
	else target->addNewBonus(std::make_shared<Bonus>(marker(0)));
	CGameState restored;
	restored.preInit(LIBRARY);
	ASSERT_NO_THROW(restored.loadFromMemory(gameState()->saveToMemory()));
	auto previous = BonusSerializationContext{true, true, false};
	expectMarkerRefusal([&] { restored.validateOwnedBonusSerialization(previous); });
	if(location == OwnedLocation::POOL)
	{
		const auto * hero = restored.getMap().tryGetFromHeroPool(pooled);
		ASSERT_NE(hero, nullptr);
		ASSERT_FALSE(hero->getExportedBonusList().empty());
		ASSERT_TRUE(newHorizonsRangefinder::isEnvelope(*hero->getExportedBonusList().back()));
	}
	oldPrefixRejected(*gameState());
	if(location == OwnedLocation::HERO || location == OwnedLocation::SLOT
		|| location == OwnedLocation::POOL || location == OwnedLocation::ARTIFACT)
		oldPrefixRejected(*map());
	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & *gameState());
	CMemorySerializer mislabeled(current.extractBuffer());
	mislabeled.iser.version = static_cast<Version>(1147);
	mislabeled.iser.loadingGamestate = true;
	CGameState failed; // Whole-world decode is not transactional; discard the fresh owner.
	failed.preInit(LIBRARY);
	mislabeled.iser.cb = &failed;
	expectMarkerRefusal([&] { mislabeled.iser & failed; });
}
INSTANTIATE_TEST_SUITE_P(OwnedPayloads, RangefinderOwnedWorldSerializationTest, ::testing::Values(
	OwnedLocation::HERO, OwnedLocation::SLOT, OwnedLocation::PLAYER, OwnedLocation::PLAYER_BATTLE,
	OwnedLocation::TEAM, OwnedLocation::GLOBAL, OwnedLocation::POOL, OwnedLocation::ARTIFACT));
}
