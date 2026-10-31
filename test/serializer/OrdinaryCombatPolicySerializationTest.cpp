/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CapturedSpecialtySnapshotFixture.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/GameSettings.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include <array>
#include <tuple>
#include <string_view>

namespace
{
using Version = ESerializationVersion;
using capturedSpecialtySnapshotTest::constructedSnapshot;
using capturedSpecialtySnapshotTest::encode;
constexpr auto combatEpoch = Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES;
constexpr std::array<Version, 6> priorEpochs{{static_cast<Version>(1145), static_cast<Version>(1146), static_cast<Version>(1147), static_cast<Version>(1148), static_cast<Version>(1149), static_cast<Version>(1150)}};
struct CombatPolicy { const char * key; const char * hero; const char * name; int value; };
constexpr std::array<CombatPolicy, 4> combatPolicies{{
	{"shieldAndSwordPhysicalReductionPercent", "core:sorsha", "Shield and Sword", 20},
	{"sepulchralGripSpeedReduction", "core:straker", "Sepulchral Grip", 1},
	{"cyclopeanBreachIgnorePercent", "core:yog", "Cyclopean Breach", 25},
	{"runicEnduranceReductionPercent", "core:ufretin", "Runic Endurance", 25}}};
JsonNode & policy(JsonNode & rules) { return rules["additionalSpecialties"]["ordinaryCreatureSpecialties"]; }
const JsonNode & policy(const JsonNode & rules) { return rules["additionalSpecialties"]["ordinaryCreatureSpecialties"]; }
void install(JsonNode & rules, const CombatPolicy & row, int value)
{
	policy(rules)[row.key].Integer() = value;
}

JsonNode priorRules(bool includeFour = false, bool includeRanged = false, bool includeMaintenance = false, bool includeMachine = false)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	for(const auto & entry : combatPolicies) policy(rules).Struct().erase(entry.key);
	if(!includeMachine)
		for(const char * key : {"gunneryProfessorInitiativeFlat", "gunneryProfessorFollowUpBonusPoints", "heavyBatterySiegeTermBonusPercent"})
			policy(rules).Struct().erase(key);
	if(!includeMaintenance)
		for(const char * key : {"fieldReassemblyHealingHP", "fieldMaintenanceHealingHP", "ogreBulwarkRetaliations"})
			policy(rules).Struct().erase(key);
	if(!includeRanged)
		for(const char * key : {"cruelVolleyDamagePercent", "rangefinderFullDamageDistance"})
			policy(rules).Struct().erase(key);
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
template<class Decode> void expectCombatReadFailure(Decode decode)
{
	try { decode(); FAIL() << "Expected1151 captured policy rejection"; }
	catch(const std::runtime_error & error)
	{
		EXPECT_EQ(std::string(error.what()), "Captured ordinary combat specialties require format1151");
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
			expectCombatReadFailure([&] { owner.serialize(wire.oser); });
			EXPECT_TRUE(wire.extractBuffer().empty());
		}
		else
		{
			ASSERT_NO_THROW(owner.serialize(wire.oser));
			EXPECT_FALSE(wire.extractBuffer().empty());
		}
	}
}
enum class Owner { ALL, HERO, POOL, SETTINGS, WORLD };
using CombatCase = std::tuple<int, bool, Owner>;
class OrdinaryCombatPolicySerializationTest : public HeroCommandFixture,
	public ::testing::WithParamInterface<CombatCase>
{
protected:
	JsonNode authored;
	bool historicFour = false;
	bool historicRanged = false;
	bool historicMaintenance = false;
	bool historicMachine = false;
	bool firstSixOnly = false;
	std::optional<int32_t> neutralRequest;
	const CombatPolicy & entry() const { return combatPolicies.at(std::get<0>(GetParam())); }
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
		authored = priorRules(historicFour, historicRanged, historicMaintenance, historicMachine);
		if(firstSixOnly)
			for(const char * key : {"massedSpearsDamagePercent", "packPursuitDamagePercent", "bloodScentDamagePercent",
				"livingBastionDefense", "chillingPresenceReductionPercent", "boneFormationDefense",
				"stoneSentinelReductionPercent", "ogreWarcallerDamagePercent", "undertowRetaliationPercent",
				"wyvernsPreyDamagePercent", "mobPressureDamagePercent", "wingbeatInitiativePercent", "measuredVolleyMdrPenetrationPercent"})
				policy(authored).Struct().erase(key);
		if(natural) install(authored, entry(), std::get<1>(GetParam()) ? 0 : entry().value);
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
TEST_P(OrdinaryCombatPolicySerializationTest, CurrentOwnedSnapshotsKeepIdentityAndPriorWritersRejectBeforePrefix)
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
			install(rules, entry(), std::get<1>(GetParam()) ? 0 : entry().value);
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
			install(rules, entry(), std::get<1>(GetParam()) ? 0 : entry().value);
			rules.setOverrideFlag(true);
			map()->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		}
		else
		{
			auto rules = world->getHeroDevelopmentRules();
			install(rules, entry(), std::get<1>(GetParam()) ? 0 : entry().value);
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
	expectOldOwner(*target, captured);
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
	//1150/1151 have identical fields here: reject on captured policy, not layout.
	if(captured) expectCombatReadFailure([&] { heroFrom(encode(*target), priorEpochs.back()); });
	const auto bytes = encode(*world);
	CGameState discarded;
	discarded.preInit(LIBRARY);
	expectCombatReadFailure([&] { discarded.loadReplaySnapshot(ReplayPayload{bytes, priorEpochs.back()}); });
	CMemorySerializer badLobby(encode(packet));
	badLobby.iser.version = priorEpochs.back();
	LobbyStartGame discardedLobby;
	expectCombatReadFailure([&] { discardedLobby.serialize(badLobby.iser); });
}
INSTANTIATE_TEST_SUITE_P(CombatAndOwnedScopes, OrdinaryCombatPolicySerializationTest,
	::testing::Combine(::testing::Range(0, 4), ::testing::Bool(),
		::testing::Values(Owner::ALL, Owner::HERO, Owner::POOL, Owner::SETTINGS, Owner::WORLD)));
class OrdinaryCombatHistoricalTest : public OrdinaryCombatPolicySerializationTest {};
TEST_P(OrdinaryCombatHistoricalTest, GenuinePriorWorldAndPoolKeepMissingCombatAndCapturedIdentity)
{
	const auto epoch = priorEpochs.at(2 * static_cast<size_t>(std::get<2>(GetParam()))
		+ static_cast<size_t>(std::get<1>(GetParam())));
	historicFour = epoch >= static_cast<Version>(1146);
	historicRanged = epoch >= static_cast<Version>(1148);
	historicMaintenance = epoch >= static_cast<Version>(1149);
	historicMachine = epoch >= static_cast<Version>(1150);
	if(epoch >= static_cast<Version>(1147)) neutralRequest = 75;
	ASSERT_NO_FATAL_FAILURE(prepare(false, entry().hero));
	const auto type = attackerSideHero->getHeroTypeID();
	const auto otherType = HeroTypeID(HeroTypeID::decode(combatPolicies[(std::get<0>(GetParam()) + 1) % 4].hero));
	auto * other = map()->tryGetFromHeroPool(otherType);
	ASSERT_NE(other, nullptr);
	for(const auto & row : combatPolicies)
	{
		ASSERT_FALSE(policy(gameState()->getHeroDevelopmentRules()).Struct().contains(row.key));
		ASSERT_FALSE(policy(attackerSideHero->getPrimaryGrowthRules()).Struct().contains(row.key));
		ASSERT_FALSE(policy(other->getPrimaryGrowthRules()).Struct().contains(row.key));
	}
	ASSERT_EQ(policy(gameState()->getHeroDevelopmentRules()), policy(authored));
	ASSERT_TRUE(policy(gameState()->getHeroDevelopmentRules()).Struct().contains("massedSpearsDamagePercent"));
	ASSERT_EQ(policy(gameState()->getHeroDevelopmentRules()).Struct().contains("prismaticScreenReductionPercent"), historicFour);
	ASSERT_EQ(policy(gameState()->getHeroDevelopmentRules()).Struct().contains("cruelVolleyDamagePercent"), historicRanged);
	ASSERT_EQ(policy(gameState()->getHeroDevelopmentRules()).Struct().contains("rangefinderFullDamageDistance"), historicRanged);
	ASSERT_EQ(policy(gameState()->getHeroDevelopmentRules()).Struct().contains("ogreBulwarkRetaliations"), historicMaintenance);
	ASSERT_EQ(policy(gameState()->getHeroDevelopmentRules()).Struct().contains("gunneryProfessorInitiativeFlat"), historicMachine);
	ASSERT_EQ(policy(gameState()->getHeroDevelopmentRules()).Struct().contains("gunneryProfessorFollowUpBonusPoints"), historicMachine);
	ASSERT_EQ(policy(gameState()->getHeroDevelopmentRules()).Struct().contains("heavyBatterySiegeTermBonusPercent"), historicMachine);
	const auto bytes = encode(*gameState(), epoch);
	if(epoch >= static_cast<Version>(1147)) EXPECT_EQ(bytes, encode(*gameState()));
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
	// must not recapture any of these four combat identities or create a new effect.
	auto defaults = gameState()->getHeroDevelopmentRules();
	for(const auto & row : combatPolicies) install(defaults, row, row.value);
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
	auto * retainedPool = newer.getMap().tryGetFromHeroPool(otherType);
	ASSERT_NE(retainedPool, nullptr);
	retainedPool->initHero(rng);
	sameHero(*other, *retainedPool);
}
INSTANTIATE_TEST_SUITE_P(FourIdentitiesAndSixPriorCohorts, OrdinaryCombatHistoricalTest,
	::testing::Combine(::testing::Range(0, 4), ::testing::Bool(), ::testing::Values(Owner::ALL, Owner::HERO, Owner::POOL)));
class OrdinaryCombatMalformedTest : public OrdinaryCombatPolicySerializationTest {};
TEST_P(OrdinaryCombatMalformedTest, InvalidCurrentHeroAndGlobalRulesRejectAfterValidDomainControls)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto & heroRules = attackerSideHero->getPrimaryGrowthRules();
	const auto & worldRules = gameState()->getHeroDevelopmentRules();
	for(const bool unknown : {false, true})
	{
		auto heroValid = heroRules;
		auto worldValid = worldRules;
		install(heroValid, entry(), 0);
		install(worldValid, entry(), 0);
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
INSTANTIATE_TEST_SUITE_P(FourMalformedOwnerDomains, OrdinaryCombatMalformedTest,
	::testing::Combine(::testing::Range(0, 4), ::testing::Values(false), ::testing::Values(Owner::HERO)));
TEST(OrdinaryCombatAdmission, NamedFlagAndExplicitZeroRemainStrictTo1151)
{
	static_assert(static_cast<int>(combatEpoch) == 1151);
	static_assert(Version::CURRENT == combatEpoch);
	EXPECT_FALSE(newHorizonsHeroes::SpecialtySerializationContext{}.ordinaryCombatSpecialties);
	for(const auto epoch : priorEpochs) EXPECT_FALSE(context(epoch).ordinaryCombatSpecialties);
	EXPECT_TRUE(context(Version::CURRENT).ordinaryCombatSpecialties);
	EXPECT_TRUE(context(static_cast<Version>(1146)).ordinaryCreaturePolicyExtensions);
	for(const auto & row : combatPolicies)
		for(const int value : {0, row.value})
		{
			auto rules = priorRules();
			install(rules, row, value);
			const auto before = rules;
			ASSERT_NO_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules));
			for(const auto epoch : priorEpochs)
				expectCombatReadFailure([&] { newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(epoch)); });
			EXPECT_EQ(rules, before);
		}
}
TEST(OrdinaryCombatAdmission, PreviousThirteenAndFourPoliciesKeepTheirOwnBoundaries)
{
	auto rules = priorRules();
	EXPECT_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1144))), std::runtime_error);
	ASSERT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1145))));
	policy(rules)["prismaticScreenReductionPercent"].Integer() = 0;
	EXPECT_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1145))), std::runtime_error);
	for(const auto epoch : {static_cast<Version>(1146), static_cast<Version>(1147), Version::CURRENT})
		EXPECT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(epoch)));
}
TEST(OrdinaryCombatAdmission, Minimal1144RejectsOnlyTheNewFourKeysIncludingZero)
{
	for(const auto & row : combatPolicies)
		for(const int value : {0, row.value})
		{
			JsonNode rules;
			rules["additionalSpecialties"]["version"].Integer() = 1;
			auto & minimal = policy(rules);
			minimal["version"].Integer() = 1;
			minimal["closeQuartersDrill"].Integer() = 1;
			minimal["marshSharpshooter"].Integer() = 1;
			for(const char * key : {"retaliationDamagePercent", "defendDefense", "maximumHealthPercent", "resistanceAuraPoints"})
				minimal[key].Integer() = 0;
			ASSERT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1144))));
			install(rules, row, value);
			ASSERT_NO_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules));
			expectCombatReadFailure([&] { newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1144))); });
		}
	auto ranged = priorRules(true, true);
	EXPECT_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(ranged, context(static_cast<Version>(1147))), std::runtime_error);
	EXPECT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(ranged, context(static_cast<Version>(1148))));
}
TEST(OrdinaryCombatAdmission, CurrentStillRejectsBadCombatShapesAndUnknownKey)
{
	for(const auto & row : combatPolicies)
		for(int kind = 0; kind < 6; ++kind)
		{
			auto rules = priorRules();
			install(rules, row, row.value);
			ASSERT_NO_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules));
			auto & bad = policy(rules)[row.key];
			bad = JsonNode();
			switch(kind)
			{
			case 0: bad.Integer() = -1; break;
			case 1: bad.Integer() = 101; break;
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
TEST(OrdinaryCombatAdmission, SettingsCurrentKeepsZeroAndOldReadRejectsBeforeInstalling)
{
	for(const auto & row : combatPolicies)
	{
		auto rules = priorRules();
		install(rules, row, 0);
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
		expectCombatReadFailure([&] { target.serialize(old.iser); });
		EXPECT_EQ(encode(target), before);
	}
}
TEST(OrdinaryCombatAdmission, FourIndependentKeysDoNotWidenAnyPriorFamilyGate)
{
	for(const auto & row : combatPolicies)
	{
		auto rules = priorRules(true, true, true, true);
		EXPECT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1150))));
		install(rules, row, 0);
		expectCombatReadFailure([&] { newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(static_cast<Version>(1150))); });
		EXPECT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(Version::CURRENT)));
	}
	EXPECT_TRUE(context(static_cast<Version>(1149)).ordinaryMaintenanceSpecialties);
	EXPECT_FALSE(context(static_cast<Version>(1149)).ordinaryMachineSpecialties);
	EXPECT_TRUE(context(static_cast<Version>(1150)).ordinaryMachineSpecialties);
	EXPECT_FALSE(context(static_cast<Version>(1150)).ordinaryCombatSpecialties);
}
class OrdinaryCombat1144OwnerTest : public OrdinaryCombatPolicySerializationTest {};
TEST_P(OrdinaryCombat1144OwnerTest, ActualFirstSixOwnersRejectNewPresenceBeforeTheirPrefixes)
{
	firstSixOnly = true;
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto reject = [](auto & owner)
	{
		CMemorySerializer wire;
		wire.oser.version = static_cast<Version>(1144);
		expectCombatReadFailure([&] { owner.serialize(wire.oser); });
		EXPECT_TRUE(wire.extractBuffer().empty());
	};
	reject(*attackerSideHero);
	reject(*map());
	reject(*gameState());
	auto packet = lobby(gameState());
	reject(packet);
	CGameState decoded;
	decoded.preInit(LIBRARY);
	ASSERT_NO_THROW(decoded.loadFromMemory(encode(*gameState())));
	ASSERT_NE(decoded.getMap().getHero(attackerSideHero->getHeroTypeID()), nullptr);
	sameHero(*attackerSideHero, *decoded.getMap().getHero(attackerSideHero->getHeroTypeID()));
}
INSTANTIATE_TEST_SUITE_P(FourKeysZeroAndPositive, OrdinaryCombat1144OwnerTest,
	::testing::Combine(::testing::Range(0, 4), ::testing::Bool(), ::testing::Values(Owner::ALL)));
}
