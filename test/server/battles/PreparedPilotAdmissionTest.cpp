/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PreparedPilotFixture.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/StartInfo.h"
#include <type_traits>
namespace
{
JsonNode & basicMechanics(JsonNode & rules)
{
	return rules["skills"]["new-horizons:discipline"]["ranks"]["basic"]["mechanics"];
}
std::vector<const Bonus *> exported(const CGHeroInstance & hero)
{
	std::vector<const Bonus *> result;
	for(const auto & bonus : hero.getExportedBonusList()) result.push_back(bonus.get());
	return result;
}
// Populate the real typed campaign codec, without exposing its private pools.
struct PilotCampaignPoolWriter
{
	using Version = ESerializationVersion;
	CMemorySerializer & wire;
	JsonNode hero;
	bool saving = true;
	bool hasFeature(Version feature) const { return wire.oser.hasFeature(feature); }
	template<typename T> PilotCampaignPoolWriter & operator&(T & value)
	{
		if constexpr(std::is_same_v<T, std::map<CampaignScenarioID, std::vector<JsonNode>>>)
		{
			auto pool = value;
			pool[CampaignScenarioID(0)] = {hero};
			wire.oser & pool;
		}
		else wire.oser & value;
		return *this;
	}
};
std::shared_ptr<CampaignState> pilotCampaign(JsonNode hero)
{
	CampaignState empty;
	CMemorySerializer wire;
	PilotCampaignPoolWriter writer{wire, std::move(hero)};
	empty.serialize(writer);
	auto captured = std::make_shared<CampaignState>();
	captured->serialize(wire.iser);
	return captured;
}
}
TEST_F(PreparedPilotFixture, InvalidRankDefinitionCannotRemoveOldBonusesOrPublishNewCache)
{
	customValues = true;
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	const SecondarySkill skill(SecondarySkill::decode("new-horizons:discipline"));
	attackerSideHero->setSecSkillLevel(skill, 1, ChangeValueMode::ABSOLUTE);
	const auto before = exported(*attackerSideHero);
	const auto * cache = attackerSideHero->getPreparedPilotPerks();
	auto & state = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
	const JsonNode admitted = state.rules;
	basicMechanics(state.rules)["version"].Integer() = 2;
	EXPECT_THROW(attackerSideHero->recreateSecondarySkillsBonuses(), std::runtime_error);
	EXPECT_THROW(attackerSideHero->rebuildPreparedNativeRules(), std::runtime_error);
	EXPECT_EQ(exported(*attackerSideHero), before);
	EXPECT_EQ(attackerSideHero->getPreparedPilotPerks(), cache);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(skill), 1);
	state.rules = admitted;
}
TEST_F(PreparedPilotFixture, HeroMapAndWorldWritersRejectMalformedCapturedRowsBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	auto & state = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
	const JsonNode admitted = state.rules;
	for(const auto * skill : {"new-horizons:discipline", "new-horizons:luck", "new-horizons:chaosMagic"})
	{
		SCOPED_TRACE(skill);
		state.rules = admitted;
		if(std::string_view(skill) == "new-horizons:chaosMagic")
		{
			auto & perks = state.rules["skills"][skill]["perks"].Vector();
			const auto perk = std::find_if(perks.begin(), perks.end(), [](const auto & row)
			{
				return row["id"].String() == "new-horizons:chaosMagic.mindbreaker";
			});
			ASSERT_NE(perk, perks.end());
			(*perk)["mechanics"]["version"].Integer() = 2;
		}
		else state.rules["skills"][skill]["ranks"]["basic"]["mechanics"]["version"].Integer() = 2;
		CMemorySerializer hero;
		EXPECT_THROW(attackerSideHero->serialize(hero.oser), std::runtime_error);
		EXPECT_TRUE(hero.extractBuffer().empty());
		CMemorySerializer map;
		EXPECT_THROW(gameState()->getMap().serialize(map.oser), std::runtime_error);
		EXPECT_TRUE(map.extractBuffer().empty());
		CMemorySerializer world;
		EXPECT_THROW(gameState()->serialize(world.oser), std::runtime_error);
		EXPECT_TRUE(world.extractBuffer().empty());
		CampaignState campaign;
		EXPECT_THROW(campaign.crossoverSerialize(attackerSideHero), std::runtime_error);
	}
	state.rules = admitted;
}
TEST_F(PreparedPilotFixture, TavernPoolAndCampaignPublicationUseTheirOwnSavedRows)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	auto & map = gameState()->getMap();
	const auto pool = map.getHeroesInPool();
	ASSERT_FALSE(pool.empty());
	auto * tavernHero = map.tryGetFromHeroPool(pool.front());
	ASSERT_NE(tavernHero, nullptr);
	auto & tavern = const_cast<newHorizonsHeroes::PerkState &>(tavernHero->getPerkState());
	const auto original = tavern;
	tavern.rules = attackerSideHero->getPerkState().rules;
	basicMechanics(tavern.rules)["version"].Integer() = 2;
	CMemorySerializer wire;
	EXPECT_THROW(map.serialize(wire.oser), std::runtime_error);
	EXPECT_TRUE(wire.extractBuffer().empty());
	tavern = original;
	CampaignState campaign;
	auto snapshot = campaign.crossoverSerialize(attackerSideHero);
	snapshot["primaryGrowthRules"]["additionalSpecialties"]["preparedCreatureSpecialties"]["heroes"]["core:fiona"]["mechanics"]["operations"].Vector()[0]["family"].String() = "core:unicorn";
	const auto objects = map.objects.size();
	const auto before = exported(*attackerSideHero);
	EXPECT_THROW(campaign.crossoverDeserialize(snapshot, &map), std::runtime_error);
	EXPECT_EQ(map.objects.size(), objects);
	EXPECT_EQ(exported(*attackerSideHero), before);
}
TEST_F(PreparedPilotFixture, PerkReaderRejectsMalformedDefinitionWithoutReplacingAdmittedState)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	auto target = attackerSideHero->getPerkState();
	const auto before = target.toJson();
	auto malformed = target.rules;
	basicMechanics(malformed)["version"].Integer() = 2;
	CMemorySerializer wire;
	wire.oser & malformed;
	wire.oser & target.selected;
	std::shared_ptr<const nativeRules::PreparedPilotPerks> prepared = target.prepareNativeRules();
	const auto previous = prepared;
	EXPECT_THROW(target.serialize(wire.iser, &prepared), std::runtime_error);
	EXPECT_EQ(target.toJson(), before);
	EXPECT_EQ(prepared, previous);
}
TEST(PreparedPilotSettings, MergedAdmissionIsAtomicAndValidPartialRowsRoundTrip)
{
	GameSettings settings;
	ASSERT_NO_THROW(settings.loadBase(LIBRARY->settingsHandler->getFullConfig()));
	const auto before = settings.getFullConfig();
	GameSettings control;
	ASSERT_NO_THROW(control.loadBase(LIBRARY->settingsHandler->getFullConfig()));
	JsonNode allowed;
	allowed["combat"]["moraleExtraDamagePercent"].Integer() = 33;
	ASSERT_NO_THROW(control.loadOverrides(allowed));
	JsonNode invalid;
	invalid["combat"]["moraleExtraDamagePercent"].Integer() = 33;
	basicMechanics(invalid["heroes"]["newHorizonsPerks"])["version"].Integer() = 2;
	EXPECT_THROW(settings.loadOverrides(invalid), std::runtime_error);
	EXPECT_EQ(settings.getFullConfig(), before);
	EXPECT_THROW(settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, invalid["heroes"]["newHorizonsPerks"]), std::runtime_error);
	EXPECT_EQ(settings.getFullConfig(), before);
	JsonNode partial;
	basicMechanics(partial)["version"].Integer() = 1;
	ASSERT_NO_THROW(settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, partial));
	JsonNode family;
	family["additionalSpecialties"]["preparedCreatureSpecialties"]["heroes"]["core:fiona"]["mechanics"]["version"].Integer() = 1;
	ASSERT_NO_THROW(settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, family));
	CMemorySerializer wire;
	ASSERT_NO_THROW(settings.serialize(wire.oser));
	GameSettings restored;
	ASSERT_NO_THROW(restored.loadBase(LIBRARY->settingsHandler->getFullConfig()));
	ASSERT_NO_THROW(restored.serialize(wire.iser));
	// Other absent versioned contexts retain their existing binary-load
	// normalization; these intentionally partial pilot rows inherit at this owner.
	EXPECT_EQ(restored.getValue(EGameSettings::HEROES_NEW_HORIZONS), settings.getValue(EGameSettings::HEROES_NEW_HORIZONS));
	EXPECT_EQ(restored.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), settings.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS));
	CMemorySerializer unsupported;
	unsupported.oser.version = static_cast<ESerializationVersion>(1156);
	EXPECT_THROW(settings.serialize(unsupported.oser), std::runtime_error);
	EXPECT_TRUE(unsupported.extractBuffer().empty());
}
TEST(PreparedPilotSettings, MalformedRawFragmentCannotReplaceExistingOverrides)
{
	GameSettings settings;
	ASSERT_NO_THROW(settings.loadBase(LIBRARY->settingsHandler->getFullConfig()));
	const auto before = settings.getFullConfig();
	for(int invalid = 0; invalid < 2; ++invalid)
	{
		JsonNode raw;
		auto & mechanics = basicMechanics(raw["heroes"]["newHorizonsPerks"]);
		if(invalid == 0) mechanics["version"].Integer() = 2;
		else mechanics["expression"].String() = "hero.level";
		CMemorySerializer wire;
		wire.oser & raw;
		EXPECT_THROW(settings.serialize(wire.iser), std::runtime_error);
		EXPECT_EQ(settings.getFullConfig(), before);
	}
}

TEST_F(PreparedPilotFixture, CampaignPoolReaderRejectsMalformedSavedRowsBeforePoolPublication)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	CampaignState campaign;
	auto snapshot = campaign.crossoverSerialize(attackerSideHero);
	basicMechanics(snapshot["perkState"]["rules"])["version"].Integer() = 2;
	std::map<CampaignScenarioID, std::vector<JsonNode>> incoming{{CampaignScenarioID(0), {snapshot}}};
	std::map<HeroTypeID, JsonNode> global;
	CMemorySerializer wire;
	wire.oser & static_cast<Campaign &>(campaign);
	wire.oser & incoming;
	wire.oser & global;
	EXPECT_THROW(campaign.serialize(wire.iser), std::runtime_error);
	EXPECT_TRUE(campaign.getHeroesByPower(CampaignScenarioID(0)).empty());
	CMemorySerializer preserved;
	EXPECT_NO_THROW(campaign.serialize(preserved.oser));
}

TEST_F(PreparedPilotFixture, WorldPilotPreflightRejectsEitherSerializedCampaignBeforeWorldPrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	auto * scenario = gameState()->getStartInfo();
	// Fixture owns mutable StartInfo copies; production preflight remains const.
	auto * initial = const_cast<StartInfo *>(gameState()->getInitialStartInfo());
	ASSERT_NE(scenario, nullptr);
	ASSERT_NE(initial, nullptr);
	ASSERT_EQ(scenario->campState, nullptr);
	ASSERT_EQ(initial->campState, nullptr);
	ASSERT_NO_THROW(gameState()->validateNativePilotSerialization(true));
	CampaignState empty;
	auto captured = pilotCampaign(empty.crossoverSerialize(attackerSideHero));
	ASSERT_EQ(captured->getHeroesByPower(CampaignScenarioID(0)).size(), 1u);
	auto & record = const_cast<JsonNode &>(captured->getHeroesByPower(CampaignScenarioID(0)).front());
	basicMechanics(record["perkState"]["rules"])["version"].Integer() = 2;
	for(auto * start : {scenario, initial})
	{
		start->campState = captured;
		EXPECT_THROW(gameState()->validateNativePilotSerialization(true), std::runtime_error);
		CMemorySerializer world;
		EXPECT_THROW(gameState()->serialize(world.oser), std::runtime_error);
		EXPECT_TRUE(world.extractBuffer().empty());
		start->campState.reset();
	}
}

TEST_F(PreparedPilotFixture, SharedCampaignPilotPreflightPreservesSerializedPoolAndStartInfoIdentity)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	auto * scenario = gameState()->getStartInfo();
	auto * initial = const_cast<StartInfo *>(gameState()->getInitialStartInfo());
	ASSERT_NE(scenario, nullptr);
	ASSERT_NE(initial, nullptr);
	CampaignState empty;
	auto captured = pilotCampaign(empty.crossoverSerialize(attackerSideHero));
	scenario->campState = initial->campState = captured;
	CMemorySerializer before;
	ASSERT_NO_THROW(captured->serialize(before.oser));
	const auto expected = before.extractBuffer();
	ASSERT_NO_THROW(gameState()->validateNativePilotSerialization(true));
	EXPECT_EQ(scenario->campState, captured);
	EXPECT_EQ(initial->campState, captured);
	CMemorySerializer after;
	ASSERT_NO_THROW(captured->serialize(after.oser));
	EXPECT_EQ(after.extractBuffer(), expected);
	scenario->campState.reset();
	initial->campState.reset();
}
