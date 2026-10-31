/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include <array>
#include <tuple>
#include "../server/battles/HeroCommandFixture.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/GameSettings.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "CapturedSpecialtySnapshotFixture.h"

namespace
{
using Version = ESerializationVersion;
constexpr std::array<Version, 2> oldEpochs{{Version::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES,
	Version::NEW_HORIZONS_ADJACENCY_FILTERS}};
struct Extension
{
	const char * key;
	const char * hero;
	const char * name;
	int value;
};
constexpr std::array<Extension, 4> extensions{{
	{"prismaticScreenReductionPercent", "core:pasis", "Prismatic Screen", 15},
	{"evasiveFlightReductionPercent", "core:korbac", "Evasive Flight", 20},
	{"ridersExploitDamagePercent", "core:tyraxor", "Rider's Exploit", 20},
	{"tidalRenewalHealingHP", "core:kalt", "Tidal Renewal", 10}}};
const JsonNode & policy(const JsonNode & rules)
{
	return rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
}
JsonNode & policy(JsonNode & rules)
{
	return rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
}
JsonNode oldRules(bool keepPreviousKeys = false)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	for(const char * key : {"shieldAndSwordPhysicalReductionPercent", "sepulchralGripSpeedReduction", "cyclopeanBreachIgnorePercent", "runicEnduranceReductionPercent"})
		policy(rules).Struct().erase(key);
	for(const char * key : {"gunneryProfessorInitiativeFlat", "gunneryProfessorFollowUpBonusPoints", "heavyBatterySiegeTermBonusPercent"})
		policy(rules).Struct().erase(key);
	for(const char * key : {"fieldReassemblyHealingHP", "fieldMaintenanceHealingHP", "ogreBulwarkRetaliations"})
		policy(rules).Struct().erase(key);
	for(const auto & entry : extensions) policy(rules).Struct().erase(entry.key);
	for(const char * key : {"cruelVolleyDamagePercent", "rangefinderFullDamageDistance"})
		policy(rules).Struct().erase(key);
	if(!keepPreviousKeys)
		for(const auto * key : {"massedSpearsDamagePercent", "packPursuitDamagePercent", "bloodScentDamagePercent",
			"livingBastionDefense", "chillingPresenceReductionPercent", "boneFormationDefense",
			"stoneSentinelReductionPercent", "ogreWarcallerDamagePercent", "undertowRetaliationPercent",
			"wyvernsPreyDamagePercent", "mobPressureDamagePercent", "wingbeatInitiativePercent",
			"measuredVolleyMdrPenetrationPercent"})
			policy(rules).Struct().erase(key);
	rules.setOverrideFlag(true);
	return rules;
}
using capturedSpecialtySnapshotTest::encode;
template<class Decode> void expect1146PolicyRejection(Decode decode)
{
	try { decode(); FAIL() << "Expected1146 policy admission rejection"; }
	catch(const std::runtime_error & error)
	{
		EXPECT_EQ(std::string(error.what()), "Captured ordinary creature policy extensions require format1146");
	}
}
template<class T> void expectOldPrefix(T & value, bool rejected)
{
	for(const auto epoch : oldEpochs)
	{
		CMemorySerializer wire;
		wire.oser.version = epoch;
		if(rejected)
		{
			EXPECT_THROW(value.serialize(wire.oser), std::runtime_error);
			EXPECT_TRUE(wire.extractBuffer().empty());
		}
		else
		{
			ASSERT_NO_THROW(value.serialize(wire.oser));
			EXPECT_FALSE(wire.extractBuffer().empty());
		}
	}
}

using capturedSpecialtySnapshotTest::constructedSnapshot;

enum class Scope { ALL, HERO, POOL, SETTINGS, WORLD };
using OwnerCase = std::tuple<int, bool, Scope>;
class OrdinaryPolicyRealOwnersTest : public HeroCommandFixture,
	public ::testing::WithParamInterface<OwnerCase>
{
protected:
	JsonNode authored;
	void mapLoaded(CMap * loaded) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, authored);
	}
	const Extension & entry() const { return extensions.at(std::get<0>(GetParam())); }
	void prepare(bool natural = false, bool keepPreviousKeys = false, const char * actor = nullptr)
	{
		authored = oldRules(keepPreviousKeys);
		if(natural) policy(authored)[entry().key].Integer() = std::get<1>(GetParam()) ? 0 : entry().value;
		const bool pool = std::get<2>(GetParam()) == Scope::POOL;
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(actor ? actor : (pool ? "core:valeska" : entry().hero))), PlayerColor(0))
			.heroExperience(0).heroGarrison({{CreatureID(0), 10}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1))
			.heroExperience(0).heroGarrison({{CreatureID(0), 10}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_TRUE(gameState()->currentBattles.empty());
	}
	LobbyStartGame lobby(std::shared_ptr<CGameState> world)
	{
		LobbyStartGame value;
		value.initializedStartInfo = std::make_shared<StartInfo>(*world->getStartInfo());
		value.initializedGameState = std::move(world);
		return value;
	}
	std::shared_ptr<CGHeroInstance> decodeHero(std::vector<std::byte> bytes, Version epoch = Version::CURRENT)
	{
		CMemorySerializer wire(std::move(bytes));
		wire.iser.version = epoch;
		wire.iser.loadingGamestate = true;
		wire.iser.cb = gameState().get();
		auto decoded = std::make_shared<CGHeroInstance>(gameState().get());
		decoded->serialize(wire.iser);
		return decoded;
	}
	void sameHero(const CGHeroInstance & before, const CGHeroInstance & after)
	{
		EXPECT_EQ(after.getHeroTypeID(), before.getHeroTypeID());
		EXPECT_EQ(after.id, before.id);
		EXPECT_EQ(after.getPrimaryGrowthRules(), before.getPrimaryGrowthRules());
		EXPECT_EQ(after.getSpecialtyPresentation().name, before.getSpecialtyPresentation().name);
		EXPECT_EQ(after.getSpecialtyPresentation().artwork, before.getSpecialtyPresentation().artwork);
		EXPECT_EQ(after.getExportedBonusList().toJsonNode(), before.getExportedBonusList().toJsonNode());
	}
};

TEST_P(OrdinaryPolicyRealOwnersTest, ActualOwnedBoundariesKeepCurrentAndRejectOldBeforePrefix)
{
	const auto scope = std::get<2>(GetParam());
	ASSERT_NO_FATAL_FAILURE(prepare(scope == Scope::ALL));
	auto world = gameState();
	const auto actorId = attackerSideHero->id;
	const auto actorType = attackerSideHero->getHeroTypeID();
	const auto poolId = HeroTypeID(HeroTypeID::decode(entry().hero));
	CGHeroInstance * captured = attackerSideHero;
	if(scope == Scope::POOL)
	{
		captured = map()->tryGetFromHeroPool(poolId);
		ASSERT_NE(captured, nullptr);
	}
	if(scope != Scope::ALL)
	{
		expectOldPrefix(*world, false);
		auto newRules = captured->getPrimaryGrowthRules();
		policy(newRules)[entry().key].Integer() = std::get<1>(GetParam()) ? 0 : entry().value;
		if(scope == Scope::HERO || scope == Scope::POOL)
		{
			auto replacement = decodeHero(constructedSnapshot(*captured, captured->getPrimaryGrowthRules(), newRules));
			ASSERT_EQ(replacement->getPrimaryGrowthRules(), newRules);
			replacement->initHero(*gameHandler->randomizer);
			ASSERT_EQ(replacement->getPrimaryGrowthRules(), newRules);
			if(scope == Scope::HERO) map()->replaceObject(actorId, replacement);
			else
			{
				ASSERT_NE(map()->tryTakeFromHeroPool(poolId), nullptr);
				map()->addToHeroPool(replacement);
			}
			captured = replacement.get();
		}
		else if(scope == Scope::SETTINGS)
		{
			// Settings stores a global registry, not resolved Hero profile rules.
			newRules = map()->getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS);
			ASSERT_NO_THROW(newHorizonsHeroes::validateHeroRules(newRules, false));
			policy(newRules)[entry().key].Integer() = std::get<1>(GetParam()) ? 0 : entry().value;
			newRules.setOverrideFlag(true);
			ASSERT_NO_THROW(newHorizonsHeroes::validateHeroRules(newRules, false));
			map()->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, newRules);
		}
		else
		{
			newRules = world->getHeroDevelopmentRules();
			policy(newRules)[entry().key].Integer() = std::get<1>(GetParam()) ? 0 : entry().value;
			auto restored = std::make_shared<CGameState>();
			restored->preInit(LIBRARY);
			restored->loadFromMemory(constructedSnapshot(*world, world->getHeroDevelopmentRules(), newRules));
			ASSERT_EQ(restored->getHeroDevelopmentRules(), newRules);
			world = std::move(restored);
			captured = world->getMap().getHero(actorType);
			ASSERT_NE(captured, nullptr);
		}
	}
	const bool heroOwned = scope == Scope::ALL || scope == Scope::HERO || scope == Scope::POOL;
	const bool settingsOwned = scope == Scope::ALL || scope == Scope::SETTINGS;
	const bool worldOwned = scope == Scope::ALL || scope == Scope::WORLD;
	EXPECT_EQ(policy(captured->getPrimaryGrowthRules()).Struct().contains(entry().key), heroOwned);
	EXPECT_EQ(policy(world->getHeroDevelopmentRules()).Struct().contains(entry().key), worldOwned);
	EXPECT_EQ(policy(world->getMap().getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS)).Struct().contains(entry().key), settingsOwned);
	if(heroOwned) EXPECT_EQ(policy(captured->getPrimaryGrowthRules())[entry().key].Integer(), std::get<1>(GetParam()) ? 0 : entry().value);
	if(heroOwned) EXPECT_EQ(captured->getSpecialtyPresentation().name, entry().name);
	// Captured identity is established before any compatibility assertion.
	EXPECT_EQ(captured->getHeroTypeID(), scope == Scope::POOL ? poolId : actorType);
	expectOldPrefix(*captured, heroOwned);
	expectOldPrefix(world->getMap(), scope != Scope::WORLD);
	expectOldPrefix(*world, true);
	auto packet = lobby(world);
	expectOldPrefix(packet, true);

	CMemorySerializer mapWire(encode(world->getMap()));
	mapWire.iser.cb = world.get();
	mapWire.iser.loadingGamestate = true;
	CMap directMap(world.get());
	ASSERT_NO_THROW(directMap.serialize(mapWire.iser));
	EXPECT_EQ(directMap.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS),
		world->getMap().getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS));
	auto * mapHero = scope == Scope::POOL ? directMap.tryGetFromHeroPool(poolId) : directMap.getHero(actorType);
	ASSERT_NE(mapHero, nullptr);
	sameHero(*captured, *mapHero);
	if(scope != Scope::WORLD)
	{
		CMemorySerializer oldMap(encode(world->getMap()));
		oldMap.iser.version = oldEpochs.back();
		oldMap.iser.cb = world.get();
		oldMap.iser.loadingGamestate = true;
		CMap discardedMap(world.get());
		EXPECT_THROW(discardedMap.serialize(oldMap.iser), std::runtime_error);
	}
	auto directHero = decodeHero(encode(*captured));
	sameHero(*captured, *directHero);
	CGameState restored;
	restored.preInit(LIBRARY);
	ASSERT_NO_THROW(restored.loadFromMemory(encode(*world)));
	EXPECT_EQ(restored.getHeroDevelopmentRules(), world->getHeroDevelopmentRules());
	auto * restoredHero = scope == Scope::POOL ? restored.getMap().tryGetFromHeroPool(poolId) : restored.getHero(actorId);
	ASSERT_NE(restoredHero, nullptr);
	sameHero(*captured, *restoredHero);
	CMemorySerializer wire(encode(packet));
	LobbyStartGame decoded;
	ASSERT_NO_THROW(decoded.serialize(wire.iser));
	ASSERT_NE(decoded.initializedGameState, nullptr);
	EXPECT_EQ(decoded.initializedGameState->getHeroDevelopmentRules(), world->getHeroDevelopmentRules());
	auto * lobbyHero = scope == Scope::POOL ? decoded.initializedGameState->getMap().tryGetFromHeroPool(poolId) : decoded.initializedGameState->getHero(actorId);
	ASSERT_NE(lobbyHero, nullptr);
	sameHero(*captured, *lobbyHero);
	// Write the explicit1146 cohort: later CURRENT adds StartInfo fields.
	// 1145 and1146 have the same field layout. On failure these fresh owners are
	// discarded; Hero/World/Lobby do not promise transactional rollback.
	if(heroOwned) EXPECT_THROW(decodeHero(encode(*captured), oldEpochs.back()), std::runtime_error);
	CGameState rejected;
	rejected.preInit(LIBRARY);
	const auto world1146 = encode(*world, Version::NEW_HORIZONS_ORDINARY_CREATURE_POLICY_EXTENSIONS);
	expect1146PolicyRejection([&] { rejected.loadReplaySnapshot(ReplayPayload{world1146, oldEpochs.back()}); });
	CMemorySerializer badLobby(encode(packet, Version::NEW_HORIZONS_ORDINARY_CREATURE_POLICY_EXTENSIONS));
	badLobby.iser.version = oldEpochs.back();
	LobbyStartGame discarded;
	expect1146PolicyRejection([&] { discarded.serialize(badLobby.iser); });
}
class OrdinaryPolicyHistoricalOwnersTest : public OrdinaryPolicyRealOwnersTest {};
TEST_P(OrdinaryPolicyHistoricalOwnersTest, OldMissingKeysKeepFullOwnerIdentityAfterCurrentDefaultsChange)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto id = attackerSideHero->id;
	const auto heroType = attackerSideHero->getHeroTypeID();
	const auto beforeRules = attackerSideHero->getPrimaryGrowthRules();
	const auto presentation = attackerSideHero->getSpecialtyPresentation();
	const auto bonuses = attackerSideHero->getExportedBonusList().toJsonNode();
	const auto poolIds = map()->getHeroesInPool();
	ASSERT_FALSE(poolIds.empty());
	const auto poolId = poolIds.front();
	auto * pool = map()->tryGetFromHeroPool(poolId);
	ASSERT_NE(pool, nullptr);
	for(const auto epoch : oldEpochs)
	{
		const auto bytes = encode(*gameState(), epoch);
		if(epoch == oldEpochs.back()) EXPECT_EQ(bytes, encode(*gameState(), Version::NEW_HORIZONS_ORDINARY_CREATURE_POLICY_EXTENSIONS));
		CGameState loaded;
		loaded.preInit(LIBRARY);
		ASSERT_NO_THROW(loaded.loadReplaySnapshot(ReplayPayload{bytes, epoch}));
		auto * hero = loaded.getMap().getHero(heroType);
		ASSERT_NE(hero, nullptr);
		sameHero(*attackerSideHero, *hero);
		auto * loadedPool = loaded.getMap().tryGetFromHeroPool(poolId);
		ASSERT_NE(loadedPool, nullptr);
		sameHero(*pool, *loadedPool);
		for(const auto & ext : extensions)
		{
			EXPECT_FALSE(policy(loaded.getHeroDevelopmentRules()).Struct().contains(ext.key));
			EXPECT_FALSE(policy(hero->getPrimaryGrowthRules()).Struct().contains(ext.key));
			EXPECT_FALSE(policy(loadedPool->getPrimaryGrowthRules()).Struct().contains(ext.key));
		}
		auto packet = lobby(gameState());
		CMemorySerializer oldLobby(encode(packet, epoch));
		oldLobby.iser.version = epoch;
		LobbyStartGame oldDecoded;
		ASSERT_NO_THROW(oldDecoded.serialize(oldLobby.iser));
		ASSERT_NE(oldDecoded.initializedGameState, nullptr);
		ASSERT_NE(oldDecoded.initializedGameState->getHero(id), nullptr);
		sameHero(*attackerSideHero, *oldDecoded.initializedGameState->getHero(id));
	}
	// Explicitly construct a newer world policy around older captured heroes.
	// Public initHero must retain their captured absence and presentation.
	auto defaults = gameState()->getHeroDevelopmentRules();
	for(const auto & ext : extensions) policy(defaults)[ext.key].Integer() = ext.value;
	CGameState newer;
	newer.preInit(LIBRARY);
	ASSERT_NO_THROW(newer.loadFromMemory(constructedSnapshot(*gameState(),
		gameState()->getHeroDevelopmentRules(), defaults)));
	ASSERT_EQ(newer.getHeroDevelopmentRules(), defaults);
	auto * retained = newer.getMap().getHero(heroType);
	ASSERT_NE(retained, nullptr);
	GameRandomizer randomizer(newer);
	randomizer.setSeed(1337);
	retained->initHero(randomizer);
	EXPECT_EQ(retained->getPrimaryGrowthRules(), beforeRules);
	EXPECT_EQ(retained->getSpecialtyPresentation().name, presentation.name);
	EXPECT_EQ(retained->getSpecialtyPresentation().artwork, presentation.artwork);
	EXPECT_EQ(retained->getExportedBonusList().toJsonNode(), bonuses);
	for(const auto & ext : extensions) EXPECT_FALSE(policy(retained->getPrimaryGrowthRules()).Struct().contains(ext.key));
}
TEST_P(OrdinaryPolicyHistoricalOwnersTest, MalformedCurrentCapturedOwnersAreRejectedOnReadAndDiscarded)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto & heroRules = attackerSideHero->getPrimaryGrowthRules();
	const auto & globalRules = gameState()->getHeroDevelopmentRules();
	ASSERT_NO_THROW(newHorizonsHeroes::validateResolvedHeroRules(heroRules));
	ASSERT_NO_THROW(newHorizonsHeroes::validateHeroRules(globalRules, false));
	for(const bool unknown : {false, true})
	{
		auto validHero = heroRules;
		policy(validHero)[entry().key].Integer() = 0;
		auto heroControl = decodeHero(constructedSnapshot(*attackerSideHero, heroRules, validHero));
		ASSERT_EQ(heroControl->getPrimaryGrowthRules(), validHero);
		auto invalidHero = validHero;
		const char * badKey = unknown ? "unreservedFutureOrdinaryKey" : entry().key;
		policy(invalidHero)[badKey].Integer() = unknown ? 0 : -1;
		const auto before = encode(*attackerSideHero);
		EXPECT_THROW(decodeHero(constructedSnapshot(*attackerSideHero, heroRules, invalidHero)), std::runtime_error);
		EXPECT_EQ(encode(*attackerSideHero), before);

		// Prove the same global-domain replacement is accepted before changing
		// only the reserved scalar or adding the unknown policy key.
		auto validGlobal = globalRules;
		policy(validGlobal)[entry().key].Integer() = 0;
		validGlobal.setOverrideFlag(true);
		ASSERT_NO_THROW(newHorizonsHeroes::validateHeroRules(validGlobal, false));
		CGameState accepted;
		accepted.preInit(LIBRARY);
		ASSERT_NO_THROW(accepted.loadFromMemory(constructedSnapshot(*gameState(), globalRules, validGlobal)));
		ASSERT_EQ(accepted.getHeroDevelopmentRules(), validGlobal);
		map()->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, validGlobal);
		CMemorySerializer control(encode(*map()));
		control.iser.cb = gameState().get();
		control.iser.loadingGamestate = true;
		CMap acceptedMap(gameState().get());
		ASSERT_NO_THROW(acceptedMap.serialize(control.iser));
		ASSERT_EQ(acceptedMap.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS), validGlobal);
		// Restore source before constructing the malformed world, so its normal
		// preflight sees only valid original state.
		map()->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, authored);
		auto invalidGlobal = validGlobal;
		policy(invalidGlobal)[badKey].Integer() = unknown ? 0 : -1;
		auto repaired = invalidGlobal;
		if(unknown) policy(repaired).Struct().erase(badKey);
		else policy(repaired)[badKey].Integer() = 0;
		ASSERT_EQ(repaired, validGlobal);
		ASSERT_NO_THROW(newHorizonsHeroes::validateHeroRules(repaired, false));
		EXPECT_THROW(newHorizonsHeroes::validateHeroRules(invalidGlobal, false), std::runtime_error);
		CGameState discarded;
		discarded.preInit(LIBRARY);
		EXPECT_THROW(discarded.loadFromMemory(constructedSnapshot(*gameState(), globalRules, invalidGlobal)), std::runtime_error);
		map()->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, invalidGlobal);
		CMemorySerializer output;
		EXPECT_THROW(map()->serialize(output.oser), std::runtime_error);
		EXPECT_TRUE(output.extractBuffer().empty());
		map()->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, authored);
	}
}
class OrdinaryPolicyFull1145OwnersTest : public OrdinaryPolicyRealOwnersTest {};
TEST_P(OrdinaryPolicyFull1145OwnersTest, FullNineteenCaptured1145WorldAndPoolRetainIdentity)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true, "core:shakti"));
	const auto epoch = Version::NEW_HORIZONS_ADJACENCY_FILTERS;
	const auto & global = gameState()->getHeroDevelopmentRules();
	ASSERT_NO_THROW(newHorizonsHeroes::validateHeroRules(global, false));
	for(const auto & ext : extensions) ASSERT_FALSE(policy(global).Struct().contains(ext.key));
	const std::array<std::tuple<const char *, const char *, const char *>, 19> previous{{
		{"core:valeska", "Close-Quarters Drill", "closeQuartersDrill"},
		{"core:wystan", "Marsh Sharpshooter", "marshSharpshooter"},
		{"core:dace", "Unbroken Challenge", "retaliationDamagePercent"},
		{"core:thunar", "Earthshaper", "defendDefense"},
		{"core:erdamon", "Stonecaller", "maximumHealthPercent"},
		{"core:clancy", "Unicorn Sanctuary", "resistanceAuraPoints"},
		{"core:shakti", "Massed Spears", "massedSpearsDamagePercent"},
		{"core:fiona", "Pack Pursuit", "packPursuitDamagePercent"},
		{"core:vokial", "Blood Scent", "bloodScentDamagePercent"},
		{"core:ryland", "Living Bastion", "livingBastionDefense"},
		{"core:charna", "Chilling Presence", "chillingPresenceReductionPercent"},
		{"core:galthran", "Bone Formation", "boneFormationDefense"},
		{"core:piquedram", "Stone Sentinel", "stoneSentinelReductionPercent"},
		{"core:vey", "Ogre Warcaller", "ogreWarcallerDamagePercent"},
		{"core:lacus", "Undertow", "undertowRetaliationPercent"},
		{"core:broghild", "Wyvern's Prey", "wyvernsPreyDamagePercent"},
		{"core:gretchin", "Mob Pressure", "mobPressureDamagePercent"},
		{"core:aeris", "Wingbeat", "wingbeatInitiativePercent"},
		{"core:theodorus", "Measured Volley", "measuredVolleyMdrPenetrationPercent"},
	}};
	for(size_t i = 6; i < previous.size(); ++i) ASSERT_TRUE(policy(global).Struct().contains(std::get<2>(previous[i])));
	const auto bytes = encode(*gameState(), epoch);
	EXPECT_EQ(bytes, encode(*gameState(), Version::NEW_HORIZONS_ORDINARY_CREATURE_POLICY_EXTENSIONS));
	CGameState loaded;
	loaded.preInit(LIBRARY);
	ASSERT_NO_THROW(loaded.loadReplaySnapshot(ReplayPayload{bytes, epoch}));
	auto packet = lobby(gameState());
	CMemorySerializer lobbyWire(encode(packet, epoch));
	lobbyWire.iser.version = epoch;
	LobbyStartGame decoded;
	ASSERT_NO_THROW(decoded.serialize(lobbyWire.iser));
	ASSERT_NE(decoded.initializedGameState, nullptr);
	EXPECT_EQ(loaded.getHeroDevelopmentRules(), global);
	EXPECT_EQ(decoded.initializedGameState->getHeroDevelopmentRules(), global);
	for(const auto & [heroName, specialtyName, coefficient] : previous)
	{
		const auto type = HeroTypeID(HeroTypeID::decode(heroName));
		auto * original = type == attackerSideHero->getHeroTypeID() ? attackerSideHero : map()->tryGetFromHeroPool(type);
		ASSERT_NE(original, nullptr) << heroName;
		ASSERT_EQ(original->getSpecialtyPresentation().name, specialtyName);
		ASSERT_TRUE(policy(original->getPrimaryGrowthRules()).Struct().contains(coefficient));
		for(size_t i = 6; i < previous.size(); ++i)
			ASSERT_TRUE(policy(original->getPrimaryGrowthRules()).Struct().contains(std::get<2>(previous[i])));
		for(const auto & ext : extensions) ASSERT_FALSE(policy(original->getPrimaryGrowthRules()).Struct().contains(ext.key));
		auto * restored = loaded.getMap().getHero(type);
		if(!restored) restored = loaded.getMap().tryGetFromHeroPool(type);
		auto * lobbyHero = decoded.initializedGameState->getMap().getHero(type);
		if(!lobbyHero) lobbyHero = decoded.initializedGameState->getMap().tryGetFromHeroPool(type);
		ASSERT_NE(restored, nullptr) << heroName;
		ASSERT_NE(lobbyHero, nullptr) << heroName;
		sameHero(*original, *restored);
		sameHero(*original, *lobbyHero);
	}
	CMemorySerializer old;
	old.oser.version = Version::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	EXPECT_THROW(gameState()->serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}
INSTANTIATE_TEST_SUITE_P(FourExtensionsAndOwnedScopes, OrdinaryPolicyRealOwnersTest,
	::testing::Combine(::testing::Range(0, 4), ::testing::Bool(),
		::testing::Values(Scope::ALL, Scope::HERO, Scope::POOL, Scope::SETTINGS, Scope::WORLD)));
INSTANTIATE_TEST_SUITE_P(FourRetainedIdentities, OrdinaryPolicyHistoricalOwnersTest,
	::testing::Combine(::testing::Range(0, 4), ::testing::Values(false), ::testing::Values(Scope::HERO)));
INSTANTIATE_TEST_SUITE_P(FullNineteen1145, OrdinaryPolicyFull1145OwnersTest,
	::testing::Values(OwnerCase{0, false, Scope::HERO}));
}
