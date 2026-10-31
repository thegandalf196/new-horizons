/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CapturedSpecialtySnapshotFixture.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/NewHorizonsRangefinder.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include <algorithm>
#include <array>
#include <tuple>

namespace
{
using Version = ESerializationVersion;
using capturedSpecialtySnapshotTest::constructedSnapshot;
using capturedSpecialtySnapshotTest::encode;
constexpr auto pairEpoch = Version::NEW_HORIZONS_ORDINARY_RANGED_SPECIALTIES;
constexpr std::array<Version, 3> priorEpochs{{static_cast<Version>(1145), static_cast<Version>(1146), static_cast<Version>(1147)}};
struct PairPolicy { const char * key; const char * hero; const char * name; int value; };
constexpr std::array<PairPolicy, 2> pairPolicies{{
	{"cruelVolleyDamagePercent", "core:jabarkas", "Cruel Volley", 20},
	{"rangefinderFullDamageDistance", "core:christian", "Rangefinder", 12}}};
JsonNode & policy(JsonNode & rules) { return rules["additionalSpecialties"]["ordinaryCreatureSpecialties"]; }
const JsonNode & policy(const JsonNode & rules) { return rules["additionalSpecialties"]["ordinaryCreatureSpecialties"]; }
JsonNode priorRules(bool includeFour = false)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	for(const char * key : {"shieldAndSwordPhysicalReductionPercent", "sepulchralGripSpeedReduction", "cyclopeanBreachIgnorePercent", "runicEnduranceReductionPercent"})
		policy(rules).Struct().erase(key);
	for(const char * key : {"gunneryProfessorInitiativeFlat", "gunneryProfessorFollowUpBonusPoints", "heavyBatterySiegeTermBonusPercent"})
		policy(rules).Struct().erase(key);
	for(const char * key : {"fieldReassemblyHealingHP", "fieldMaintenanceHealingHP", "ogreBulwarkRetaliations"})
		policy(rules).Struct().erase(key);
	for(const auto & entry : pairPolicies) policy(rules).Struct().erase(entry.key);
	if(!includeFour)
		for(const char * key : {"prismaticScreenReductionPercent", "evasiveFlightReductionPercent",
			"ridersExploitDamagePercent", "tidalRenewalHealingHP"}) policy(rules).Struct().erase(key);
	rules.setOverrideFlag(true);
	return rules;
}
newHorizonsHeroes::SpecialtySerializationContext context(Version epoch)
{
	CMemorySerializer wire;
	wire.oser.version = epoch;
	return newHorizonsHeroes::SpecialtySerializationContext::fromHandler(wire.oser);
}
template<class Decode> void expectPairReadFailure(Decode decode)
{
	try { decode(); FAIL() << "Expected1148 captured policy rejection"; }
	catch(const std::runtime_error & error)
	{
		EXPECT_EQ(std::string(error.what()), "Captured ordinary ranged specialties require format1148");
	}
}
bool ownsRangefinderMarker(const CGHeroInstance & hero)
{
	const auto & bonuses = hero.getExportedBonusList();
	return std::any_of(bonuses.begin(), bonuses.end(), [](const auto & bonus)
	{
		return bonus && newHorizonsRangefinder::isEnvelope(*bonus);
	});
}
template<class Decode> void expectOwnedCapabilityFailure(Decode decode, bool ownsMarker)
{
	try { decode(); FAIL() << "Expected1148 owned capability rejection"; }
	catch(const std::runtime_error & error)
	{
		// Hero's owned Bonus preflight/decode precedes captured rules. A map/world
		// can reject its own captured policy first; only an actual owned
		// reserved marker permits the alternate child-capability error.
		const std::string message = error.what();
		EXPECT_TRUE(message == "Captured ordinary ranged specialties require format1148"
			|| (ownsMarker && message == "Unsupported Rangefinder envelope bonus format")) << message;
	}
}
template<class T> void expectOldOwner(T & owner, bool reject)
{
	for(const auto epoch : priorEpochs)
	{
		CMemorySerializer wire;
		wire.oser.version = epoch;
		if(reject)
		{
			expectPairReadFailure([&] { owner.serialize(wire.oser); });
			EXPECT_TRUE(wire.extractBuffer().empty());
		}
		else
		{
			ASSERT_NO_THROW(owner.serialize(wire.oser));
			EXPECT_FALSE(wire.extractBuffer().empty());
		}
	}
}
void expectOldHeroOwner(CGHeroInstance & hero, bool reject)
{
	if(!reject || !ownsRangefinderMarker(hero))
	{
		expectOldOwner(hero, reject);
		return;
	}
	for(const auto epoch : priorEpochs)
	{
		CMemorySerializer wire;
		wire.oser.version = epoch;
		expectOwnedCapabilityFailure([&] { hero.serialize(wire.oser); }, true);
		EXPECT_TRUE(wire.extractBuffer().empty());
	}
}
enum class Owner { ALL, HERO, POOL, SETTINGS, WORLD };
using PairCase = std::tuple<int, bool, Owner>;
class OrdinaryRangedPolicySerializationTest : public HeroCommandFixture,
	public ::testing::WithParamInterface<PairCase>
{
protected:
	JsonNode authored;
	bool historicFour = false;
	std::optional<int32_t> neutralRequest;
	const PairPolicy & entry() const { return pairPolicies.at(std::get<0>(GetParam())); }
	void configureWorldInitialization(StartInfo & info, GameRandomizer & rng) const override
	{
		info.extraOptionsInfo.neutralArmySizePercent = neutralRequest;
		rng.setSeed(1337);
	}
	void mapLoaded(CMap * loaded) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, authored);
	}
	void prepare(bool natural = false, const char * actorOverride = nullptr)
	{
		authored = priorRules(historicFour);
		if(natural) policy(authored)[entry().key].Integer() = std::get<1>(GetParam()) ? 0 : entry().value;
		const char * actor = actorOverride ? actorOverride : (std::get<2>(GetParam()) == Owner::POOL ? "core:valeska" : entry().hero);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(actor)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{CreatureID(0), 7}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{CreatureID(0), 7}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		ASSERT_NE(attackerSideHero, nullptr);
	}
	std::shared_ptr<CGHeroInstance> heroFrom(std::vector<std::byte> bytes, Version epoch = Version::CURRENT)
	{
		CMemorySerializer wire(std::move(bytes));
		wire.iser.version = epoch;
		wire.iser.cb = gameState().get();
		wire.iser.loadingGamestate = true;
		auto hero = std::make_shared<CGHeroInstance>(gameState().get());
		hero->serialize(wire.iser);
		return hero;
	}
	LobbyStartGame lobby(std::shared_ptr<CGameState> world)
	{
		LobbyStartGame pack;
		pack.initializedStartInfo = std::make_shared<StartInfo>(*world->getStartInfo());
		pack.initializedGameState = std::move(world);
		return pack;
	}
	void sameHero(const CGHeroInstance & source, const CGHeroInstance & decoded)
	{
		EXPECT_EQ(decoded.id, source.id);
		EXPECT_EQ(decoded.getHeroTypeID(), source.getHeroTypeID());
		EXPECT_EQ(decoded.getPrimaryGrowthRules(), source.getPrimaryGrowthRules());
		EXPECT_EQ(decoded.getExportedBonusList().toJsonNode(), source.getExportedBonusList().toJsonNode());
		EXPECT_EQ(decoded.getSpecialtyPresentation().name, source.getSpecialtyPresentation().name);
		EXPECT_EQ(decoded.getSpecialtyPresentation().description, source.getSpecialtyPresentation().description);
		EXPECT_EQ(decoded.getSpecialtyPresentation().artwork, source.getSpecialtyPresentation().artwork);
	}
};
TEST_P(OrdinaryRangedPolicySerializationTest, CurrentOwnedSnapshotsKeepIdentityAndPriorWritersRejectBeforePrefix)
{
	const auto owner = std::get<2>(GetParam());
	ASSERT_NO_FATAL_FAILURE(prepare(owner == Owner::ALL));
	auto world = gameState();
	const auto actorId = attackerSideHero->id;
	const auto actorType = attackerSideHero->getHeroTypeID();
	const auto poolType = HeroTypeID(HeroTypeID::decode(entry().hero));
	auto * target = owner == Owner::POOL ? map()->tryGetFromHeroPool(poolType) : attackerSideHero;
	ASSERT_NE(target, nullptr);
	if(owner != Owner::ALL)
	{
		expectOldOwner(*world, false);
		if(owner == Owner::HERO || owner == Owner::POOL)
		{
			auto rules = target->getPrimaryGrowthRules();
			policy(rules)[entry().key].Integer() = std::get<1>(GetParam()) ? 0 : entry().value;
			auto changed = heroFrom(constructedSnapshot(*target, target->getPrimaryGrowthRules(), rules));
			ASSERT_EQ(changed->getPrimaryGrowthRules(), rules);
			changed->initHero(*gameHandler->randomizer);
			ASSERT_EQ(changed->getPrimaryGrowthRules(), rules);
			if(owner == Owner::HERO) map()->replaceObject(actorId, changed);
			else
			{
				ASSERT_NE(map()->tryTakeFromHeroPool(poolType), nullptr);
				map()->addToHeroPool(changed);
			}
			target = changed.get();
		}
		else if(owner == Owner::SETTINGS)
		{
			auto rules = map()->getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS);
			policy(rules)[entry().key].Integer() = std::get<1>(GetParam()) ? 0 : entry().value;
			rules.setOverrideFlag(true);
			map()->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		}
		else
		{
			auto rules = world->getHeroDevelopmentRules();
			policy(rules)[entry().key].Integer() = std::get<1>(GetParam()) ? 0 : entry().value;
			auto changed = std::make_shared<CGameState>();
			changed->preInit(LIBRARY);
			changed->loadFromMemory(constructedSnapshot(*world, world->getHeroDevelopmentRules(), rules));
			ASSERT_EQ(changed->getHeroDevelopmentRules(), rules);
			world = std::move(changed);
			target = world->getMap().getHero(actorType);
		}
	}
	ASSERT_NE(target, nullptr);
	const bool captured = owner == Owner::ALL || owner == Owner::HERO || owner == Owner::POOL;
	EXPECT_EQ(policy(target->getPrimaryGrowthRules()).Struct().contains(entry().key), captured);
	EXPECT_EQ(policy(world->getHeroDevelopmentRules()).Struct().contains(entry().key), owner == Owner::ALL || owner == Owner::WORLD);
	EXPECT_EQ(policy(world->getMap().getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS)).Struct().contains(entry().key), owner == Owner::ALL || owner == Owner::SETTINGS);
	if(captured)
	{
		EXPECT_EQ(target->getHeroTypeID(), poolType);
		EXPECT_EQ(target->getSpecialtyPresentation().name, entry().name);
		EXPECT_EQ(policy(target->getPrimaryGrowthRules())[entry().key].Integer(), std::get<1>(GetParam()) ? 0 : entry().value);
	}
	const bool ownsMarker = ownsRangefinderMarker(*target);
	ASSERT_EQ(ownsMarker, captured && std::string_view(entry().key) == "rangefinderFullDamageDistance");
	expectOldHeroOwner(*target, captured);
	expectOldOwner(world->getMap(), owner != Owner::WORLD);
	expectOldOwner(*world, true);
	auto packet = lobby(world);
	expectOldOwner(packet, true);
	CMemorySerializer mapWire(encode(world->getMap()));
	mapWire.iser.cb = world.get();
	mapWire.iser.loadingGamestate = true;
	CMap directMap(world.get());
	ASSERT_NO_THROW(directMap.serialize(mapWire.iser));
	EXPECT_EQ(directMap.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS),
		world->getMap().getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS));
	auto * mapHero = owner == Owner::POOL ? directMap.tryGetFromHeroPool(poolType) : directMap.getHero(actorType);
	ASSERT_NE(mapHero, nullptr);
	sameHero(*target, *mapHero);
	auto direct = heroFrom(encode(*target));
	sameHero(*target, *direct);
	CGameState decoded;
	decoded.preInit(LIBRARY);
	ASSERT_NO_THROW(decoded.loadFromMemory(encode(*world)));
	EXPECT_EQ(decoded.getHeroDevelopmentRules(), world->getHeroDevelopmentRules());
	EXPECT_EQ(decoded.getMap().getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS), world->getMap().getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS));
	auto * decodedHero = owner == Owner::POOL ? decoded.getMap().tryGetFromHeroPool(poolType) : decoded.getMap().getHero(actorType);
	ASSERT_NE(decodedHero, nullptr);
	sameHero(*target, *decodedHero);
	CMemorySerializer lobbyWire(encode(packet));
	LobbyStartGame lobbyDecoded;
	ASSERT_NO_THROW(lobbyDecoded.serialize(lobbyWire.iser));
	ASSERT_NE(lobbyDecoded.initializedGameState, nullptr);
	EXPECT_EQ(lobbyDecoded.initializedGameState->getHeroDevelopmentRules(), world->getHeroDevelopmentRules());
	EXPECT_EQ(lobbyDecoded.initializedGameState->getMap().getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS), world->getMap().getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS));
	auto * lobbyHero = owner == Owner::POOL ? lobbyDecoded.initializedGameState->getMap().tryGetFromHeroPool(poolType) : lobbyDecoded.initializedGameState->getMap().getHero(actorType);
	ASSERT_NE(lobbyHero, nullptr);
	sameHero(*target, *lobbyHero);
	// Identical layouts must reject on policy or actual owned capability, not layout.
	if(captured) expectOwnedCapabilityFailure([&] { heroFrom(encode(*target), priorEpochs.back()); }, ownsMarker);
	const auto bytes = encode(*world);
	CGameState discarded;
	discarded.preInit(LIBRARY);
	expectOwnedCapabilityFailure([&] { discarded.loadReplaySnapshot(ReplayPayload{bytes, priorEpochs.back()}); }, ownsMarker);
	CMemorySerializer badLobby(encode(packet));
	badLobby.iser.version = priorEpochs.back();
	LobbyStartGame discardedLobby;
	expectOwnedCapabilityFailure([&] { discardedLobby.serialize(badLobby.iser); }, ownsMarker);
}
INSTANTIATE_TEST_SUITE_P(PairAndOwnedScopes, OrdinaryRangedPolicySerializationTest,
	::testing::Combine(::testing::Range(0, 2), ::testing::Bool(),
		::testing::Values(Owner::ALL, Owner::HERO, Owner::POOL, Owner::SETTINGS, Owner::WORLD)));
class OrdinaryRangedHistoricalTest : public OrdinaryRangedPolicySerializationTest {};
TEST_P(OrdinaryRangedHistoricalTest, GenuinePriorWorldAndPoolKeepMissingPairAndCapturedIdentity)
{
	const auto epoch = priorEpochs.at(static_cast<size_t>(std::get<2>(GetParam())));
	historicFour = epoch >= static_cast<Version>(1146);
	if(epoch == static_cast<Version>(1147)) neutralRequest = 75;
	ASSERT_NO_FATAL_FAILURE(prepare(false, entry().hero));
	const auto type = attackerSideHero->getHeroTypeID();
	const auto otherType = HeroTypeID(HeroTypeID::decode(pairPolicies[1 - std::get<0>(GetParam())].hero));
	auto * other = map()->tryGetFromHeroPool(otherType);
	ASSERT_NE(other, nullptr);
	for(const auto & row : pairPolicies)
	{
		ASSERT_FALSE(policy(gameState()->getHeroDevelopmentRules()).Struct().contains(row.key));
		ASSERT_FALSE(policy(attackerSideHero->getPrimaryGrowthRules()).Struct().contains(row.key));
		ASSERT_FALSE(policy(other->getPrimaryGrowthRules()).Struct().contains(row.key));
	}
	ASSERT_TRUE(policy(gameState()->getHeroDevelopmentRules()).Struct().contains("massedSpearsDamagePercent"));
	ASSERT_EQ(policy(gameState()->getHeroDevelopmentRules()).Struct().contains("prismaticScreenReductionPercent"), historicFour);
	const auto bytes = encode(*gameState(), epoch);
	if(epoch == static_cast<Version>(1147)) EXPECT_EQ(bytes, encode(*gameState()));
	CGameState decoded;
	decoded.preInit(LIBRARY);
	ASSERT_NO_THROW(decoded.loadReplaySnapshot(ReplayPayload{bytes, epoch}));
	ASSERT_NE(decoded.getMap().getHero(type), nullptr);
	ASSERT_NE(decoded.getMap().tryGetFromHeroPool(otherType), nullptr);
	sameHero(*attackerSideHero, *decoded.getMap().getHero(type));
	sameHero(*other, *decoded.getMap().tryGetFromHeroPool(otherType));
	EXPECT_EQ(decoded.getStartInfo()->extraOptionsInfo.neutralArmySizePercent, neutralRequest);
	auto packet = lobby(gameState());
	CMemorySerializer wire(encode(packet, epoch));
	wire.iser.version = epoch;
	LobbyStartGame lobbyDecoded;
	ASSERT_NO_THROW(lobbyDecoded.serialize(wire.iser));
	ASSERT_NE(lobbyDecoded.initializedGameState, nullptr);
	ASSERT_NE(lobbyDecoded.initializedGameState->getMap().getHero(type), nullptr);
	sameHero(*attackerSideHero, *lobbyDecoded.initializedGameState->getMap().getHero(type));
	// Construct newer defaults around genuine older captures. Public refresh
	// must not recapture new identity or add Christian's new envelope marker.
	auto defaults = gameState()->getHeroDevelopmentRules();
	for(const auto & row : pairPolicies) policy(defaults)[row.key].Integer() = row.value;
	CGameState newer;
	newer.preInit(LIBRARY);
	ASSERT_NO_THROW(newer.loadFromMemory(constructedSnapshot(*gameState(), gameState()->getHeroDevelopmentRules(), defaults)));
	ASSERT_EQ(newer.getHeroDevelopmentRules(), defaults);
	auto * retained = newer.getMap().getHero(type);
	ASSERT_NE(retained, nullptr);
	GameRandomizer rng(newer);
	rng.setSeed(1337);
	retained->initHero(rng);
	sameHero(*attackerSideHero, *retained);
}
INSTANTIATE_TEST_SUITE_P(TwoIdentitiesAndPriorCohorts, OrdinaryRangedHistoricalTest,
	::testing::Combine(::testing::Range(0, 2), ::testing::Values(false), ::testing::Values(Owner::ALL, Owner::HERO, Owner::POOL)));
class OrdinaryRangedMalformedTest : public OrdinaryRangedPolicySerializationTest {};
TEST_P(OrdinaryRangedMalformedTest, InvalidCurrentHeroAndGlobalRulesRejectAfterValidDomainControls)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto & heroRules = attackerSideHero->getPrimaryGrowthRules();
	const auto & worldRules = gameState()->getHeroDevelopmentRules();
	for(const bool unknown : {false, true})
	{
		auto heroValid = heroRules;
		auto worldValid = worldRules;
		policy(heroValid)[entry().key].Integer() = 0;
		policy(worldValid)[entry().key].Integer() = 0;
		auto goodHero = heroFrom(constructedSnapshot(*attackerSideHero, heroRules, heroValid));
		ASSERT_EQ(goodHero->getPrimaryGrowthRules(), heroValid);
		CGameState goodWorld;
		goodWorld.preInit(LIBRARY);
		ASSERT_NO_THROW(goodWorld.loadFromMemory(constructedSnapshot(*gameState(), worldRules, worldValid)));
		ASSERT_EQ(goodWorld.getHeroDevelopmentRules(), worldValid);
		const char * key = unknown ? "unreservedFutureRangedKey" : entry().key;
		policy(heroValid)[key].Integer() = unknown ? 0 : -1;
		policy(worldValid)[key].Integer() = unknown ? 0 : -1;
		const auto before = encode(*attackerSideHero);
		EXPECT_THROW(heroFrom(constructedSnapshot(*attackerSideHero, heroRules, heroValid)), std::runtime_error);
		CGameState discarded;
		discarded.preInit(LIBRARY);
		EXPECT_THROW(discarded.loadFromMemory(constructedSnapshot(*gameState(), worldRules, worldValid)), std::runtime_error);
		EXPECT_EQ(encode(*attackerSideHero), before);
	}
}
INSTANTIATE_TEST_SUITE_P(TwoMalformedOwnerDomains, OrdinaryRangedMalformedTest,
	::testing::Combine(::testing::Range(0, 2), ::testing::Values(false), ::testing::Values(Owner::HERO)));
TEST(OrdinaryRangedAdmission, NamedFlagAndExplicitZeroRemainStrictTo1148)
{
	static_assert(static_cast<int>(pairEpoch) == 1148);
	static_assert(Version::CURRENT >= pairEpoch);
	EXPECT_FALSE(newHorizonsHeroes::SpecialtySerializationContext{}.ordinaryRangedSpecialties);
	for(const auto epoch : priorEpochs) EXPECT_FALSE(context(epoch).ordinaryRangedSpecialties);
	EXPECT_TRUE(context(Version::CURRENT).ordinaryRangedSpecialties);
	EXPECT_TRUE(context(static_cast<Version>(1146)).ordinaryCreaturePolicyExtensions);
	for(const auto & row : pairPolicies)
		for(const int value : {0, row.value})
		{
			auto rules = priorRules();
			policy(rules)[row.key].Integer() = value;
			const auto before = rules;
			ASSERT_NO_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules));
			for(const auto epoch : priorEpochs)
				expectPairReadFailure([&] { newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(epoch)); });
			EXPECT_EQ(rules, before);
		}
}
TEST(OrdinaryRangedAdmission, PreviousThirteenAndFourPoliciesKeepTheirOwnBoundaries)
{
	auto rules = priorRules();
	EXPECT_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1144))), std::runtime_error);
	ASSERT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1145))));
	policy(rules)["prismaticScreenReductionPercent"].Integer() = 0;
	EXPECT_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1145))), std::runtime_error);
	for(const auto epoch : {static_cast<Version>(1146), static_cast<Version>(1147), Version::CURRENT})
		EXPECT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(epoch)));
}
TEST(OrdinaryRangedAdmission, CurrentStillRejectsBadPairShapesAndUnknownKey)
{
	for(const auto & row : pairPolicies)
		for(int kind = 0; kind < 6; ++kind)
		{
			auto rules = priorRules();
			policy(rules)[row.key].Integer() = row.value;
			ASSERT_NO_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules));
			auto & bad = policy(rules)[row.key];
			bad = JsonNode();
			switch(kind)
			{
			case 0: bad.Integer() = -1; break;
			case 1: bad.Integer() = std::string_view(row.key) == "cruelVolleyDamagePercent" ? 101 : 1; break;
			case 2: bad.Bool() = false; break;
			case 3: break;
			case 4: bad.Float() = 1.5; break;
			case 5: bad.String() = "12"; break;
			}
			EXPECT_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules), std::runtime_error);
		}
	auto unknown = priorRules();
	policy(unknown)["unreservedFutureRangedKey"].Integer() = 0;
	EXPECT_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(unknown), std::runtime_error);
}
TEST(OrdinaryRangedAdmission, SettingsCurrentKeepsZeroAndOldReadRejectsBeforeInstalling)
{
	for(const auto & row : pairPolicies)
	{
		auto rules = priorRules();
		policy(rules)[row.key].Integer() = 0;
		GameSettings source;
		source.addOverride(EGameSettings::HEROES_NEW_HORIZONS, rules);
		expectOldOwner(source, true);
		const auto bytes = encode(source);
		CMemorySerializer good(bytes);
		GameSettings current;
		ASSERT_NO_THROW(current.serialize(good.iser));
		EXPECT_EQ(current.getValue(EGameSettings::HEROES_NEW_HORIZONS), rules);
		GameSettings target;
		target.addOverride(EGameSettings::HEROES_NEW_HORIZONS, priorRules());
		const auto before = encode(target);
		CMemorySerializer old(bytes);
		old.iser.version = priorEpochs.back();
		expectPairReadFailure([&] { target.serialize(old.iser); });
		EXPECT_EQ(encode(target), before);
	}
}
}
