/*
 * NewHorizonsManaRegenerationArtifactTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/entities/artifact/CArtifact.h"
#include "../../../lib/entities/artifact/CArtifactInstance.h"
#include "../../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../battles/FullGameSnapshotTypes.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/ArtifactLocation.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#ifdef ENABLE_NULLKILLER2_AI
#include "../../../AI/Nullkiller2/AIUtility.h"
#endif

namespace
{
constexpr std::array<const char *, 3> ITEMS = {
	"core:charmOfMana", "core:talismanOfMana", "core:mysticOrbOfMana"};

ArtifactID item(size_t index)
{
	return ArtifactID(ArtifactID::decode(ITEMS.at(index)));
}

JsonNode tierRules()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsCapabilities"));
	// No test-only activation: root registration must publish the actual table.
	if(!rules.isStruct() || !rules.Struct().contains("artifactManaRegeneration"))
		throw std::runtime_error("Shipped Mana regeneration tiers are missing");
	// This feature's pre-Mana format controls must not contain the later Glyphs field.
	rules.Struct().erase("glyphsOfFearAura");
	return rules;
}

struct PrefixProbe
{
	using Version = ESerializationVersion;
	bool saving = true;
	bool loadingGamestate = false;
	size_t fields = 0;
	bool hasFeature(Version feature) const
	{
		return feature != Version::NEW_HORIZONS_ARTIFACT_MANA_REGENERATION;
	}
	template<class T> PrefixProbe & operator&(T &) { ++fields; return *this; }
};

class NewHorizonsManaRegenerationArtifactTest : public TinyMapGameTest
{
protected:
	bool legacy = false;
	bool original = false;
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		auto rules = original ? JsonNode() : tierRules();
		if(legacy)
			rules.Struct().erase("artifactManaRegeneration");
		if(!original)
			rules.setOverrideFlag(true); // Captured historical object replaces current defaults, not a partial merge.
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, rules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			original ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			original ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	CGHeroInstance * prepare(int knowledge = 40, bool all = true, bool duplicate = false, bool spellbook = false)
	{
		std::vector<std::pair<ArtifactPosition, ArtifactID>> equipped = {
			{ArtifactPosition::MISC1, item(0)}};
		if(spellbook)
			equipped.emplace_back(ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK);
		if(all)
		{
			equipped.emplace_back(ArtifactPosition::MISC2, item(1));
			equipped.emplace_back(ArtifactPosition::MISC3, item(2));
		}
		if(duplicate)
			equipped.emplace_back(ArtifactPosition::MISC4, item(0));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("ManaRecoveryTiers").playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
			.heroPrimary(0, 0, 0, knowledge).heroEquipped(std::move(equipped));
		startWithMap(std::move(builder));
		return findHeroAt({5, 5, 0});
	}
	int expected(const CGHeroInstance & hero) const
	{
		BonusList other;
		int64_t converted = 0;
		const auto recoveryBonuses = hero.getAllBonuses(Selector::type()(BonusType::MANA_REGENERATION));
		for(const auto & bonus : *recoveryBonuses)
		{
			const auto amount = bonus->source == BonusSource::ARTIFACT && bonus->valType == BonusValueType::BASE_NUMBER
				? newHorizonsHeroes::capabilityArtifactManaRegeneration(hero.getCapabilityRules(),
					bonus->sid.as<ArtifactID>(), hero.manaLimit()) : std::nullopt;
			if(amount)
				converted += *amount;
			else
				other.push_back(bonus);
		}
		return static_cast<int>(std::min<int64_t>(std::numeric_limits<int>::max(),
			std::max<int64_t>(converted + other.totalValue(),
				static_cast<int64_t>(hero.manaLimit()) * hero.valOfBonuses(BonusType::MANA_PERCENTAGE_REGENERATION) / 100)));
	}
};
}

TEST_F(NewHorizonsManaRegenerationArtifactTest, ThreeInstalledTiersUseMinimumPercentageAndFloor)
{
	const auto rules = tierRules();
	for(size_t index = 0; index < ITEMS.size(); ++index)
	{
		const int tier = static_cast<int>(index + 1);
		EXPECT_EQ(newHorizonsHeroes::capabilityArtifactManaRegeneration(rules, item(index), 0), tier * 5);
		EXPECT_EQ(newHorizonsHeroes::capabilityArtifactManaRegeneration(rules, item(index), 99), tier * 5);
		EXPECT_EQ(newHorizonsHeroes::capabilityArtifactManaRegeneration(rules, item(index), 203), 203 * tier * 5 / 100);
	}
	EXPECT_FALSE(newHorizonsHeroes::capabilityArtifactManaRegeneration(rules, ArtifactID::SPELLBOOK, 203));
}

TEST_F(NewHorizonsManaRegenerationArtifactTest, ActualEquipmentAddsTiersWithoutChangingDefinitions)
{
	auto * hero = prepare(200);
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->manaRegain(), expected(*hero));
	int originalValues = 0;
	const auto recoveryBonuses = hero->getAllBonuses(Selector::type()(BonusType::MANA_REGENERATION));
	for(const auto & bonus : *recoveryBonuses)
		if(bonus->source == BonusSource::ARTIFACT)
			originalValues += bonus->val;
	EXPECT_EQ(originalValues, 6);
	EXPECT_GE(hero->manaRegain(), 60);
}

TEST_F(NewHorizonsManaRegenerationArtifactTest, MinimumAndDuplicateEquipmentKeepExistingGraphStacking)
{
	auto * hero = prepare(0, true, true);
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->manaRegain(), expected(*hero));
	const auto components = hero->getAllBonuses(Selector::type()(BonusType::MANA_REGENERATION)
		.And(Selector::source(BonusSource::ARTIFACT, BonusSourceID(item(0)))));
	ASSERT_FALSE(components->empty());
	// The replacement follows exactly however many eligible components the
	// existing graph retains; no new per-instance duplication is introduced.
	EXPECT_EQ(newHorizonsHeroes::capabilityArtifactManaRegeneration(hero->getCapabilityRules(), item(0), 0), 5);
}

TEST_F(NewHorizonsManaRegenerationArtifactTest, RemovingEquipmentStopsItsConvertedRecovery)
{
	auto * hero = prepare(40, false);
	ASSERT_NE(hero, nullptr);
	const int before = hero->manaRegain();
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	handler.removeArtifact(ArtifactLocation(hero->id, ArtifactPosition::MISC1));
	EXPECT_EQ(hero->manaRegain(), expected(*hero));
	EXPECT_LT(hero->manaRegain(), before);
}

TEST_F(NewHorizonsManaRegenerationArtifactTest, ActualNewTurnUsesSharedRecoveryAndPreservesBuffer)
{
	auto * hero = prepare(100);
	ASSERT_NE(hero, nullptr);
	SetMana empty(hero->id, SetMana::Operation::SET_NORMAL, 0);
	gameState()->apply(empty);
	ASSERT_TRUE(hero->grantBufferSpellPoints(7));
	const int recovery = hero->manaRegain();
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	handler.onNewTurn();
	EXPECT_EQ(hero->getNormalSpellPoints(), std::min(hero->manaLimit(), recovery));
	EXPECT_EQ(hero->getBufferSpellPoints(), 7);
	SetMana almostFull(hero->id, SetMana::Operation::SET_NORMAL, hero->manaLimit() - 1);
	gameState()->apply(almostFull);
	EXPECT_EQ(hero->getManaNewTurn(), hero->manaLimit());
}

TEST_F(NewHorizonsManaRegenerationArtifactTest, UnrelatedFlatAndPercentageSourcesKeepExistingCompetition)
{
	auto * hero = prepare(100);
	ASSERT_NE(hero, nullptr);
	auto flat = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MANA_REGENERATION,
		BonusSource::OTHER, 17, BonusSourceID());
	auto percentage = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MANA_PERCENTAGE_REGENERATION,
		BonusSource::OTHER, 80, BonusSourceID());
	hero->addNewBonus(flat);
	hero->addNewBonus(percentage);
	EXPECT_EQ(hero->manaRegain(), expected(*hero));
	EXPECT_EQ(flat->val, 17);
	EXPECT_EQ(percentage->val, 80);
}

TEST_F(NewHorizonsManaRegenerationArtifactTest, CapturedOlderContextDoesNotRecaptureInstalledTiers)
{
	legacy = true;
	auto * hero = prepare();
	ASSERT_NE(hero, nullptr);
	EXPECT_FALSE(hero->getCapabilityRules().Struct().contains("artifactManaRegeneration"));
	const int oldRecovery = hero->manaRegain();
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	hero->initHero(*handler.randomizer);
	EXPECT_FALSE(hero->getCapabilityRules().Struct().contains("artifactManaRegeneration"));
	EXPECT_EQ(hero->manaRegain(), oldRecovery);
	EXPECT_EQ(oldRecovery, expected(*hero));
}

TEST_F(NewHorizonsManaRegenerationArtifactTest, ReinitializationPreservesConfiguredMultiplicityAndRestoresOnlyMissingRow)
{
	auto * hero = prepare();
	ASSERT_NE(hero, nullptr);
	auto rows = gameState()->getSettings().getValue(EGameSettings::BONUSES_PER_HERO);
	JsonNode extra;
	extra["type"].String() = "MANA_REGENERATION";
	extra["val"].Integer() = 2;
	extra["valueType"].String() = "BASE_NUMBER";
	rows["fixtureRecoveryA"] = extra;
	rows["fixtureRecoveryB"] = extra;
	rows.setOverrideFlag(true);
	map()->overrideGameSetting(EGameSettings::BONUSES_PER_HERO, rows);
	std::array<int, 4> primary;
	for(size_t index = 0; index < primary.size(); ++index)
		primary[index] = hero->getPrimSkillLevel(PrimarySkill(index));
	const auto localRows = [&]()
	{
		std::vector<std::shared_ptr<Bonus>> result;
		for(const auto & bonus : hero->getExportedBonusList())
			if(bonus->source == BonusSource::HERO_BASE_SKILL && bonus->sid == BonusSourceID(hero->id)
				&& bonus->duration == BonusDuration::PERMANENT && bonus->type == BonusType::MANA_REGENERATION
				&& bonus->val == 2 && bonus->valType == BonusValueType::BASE_NUMBER)
				result.push_back(bonus);
		return result;
	};
	ASSERT_TRUE(localRows().empty());
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	hero->initHero(*handler.randomizer);
	ASSERT_EQ(localRows().size(), 2);
	const int recovery = hero->manaRegain();
	hero->initHero(*handler.randomizer);
	ASSERT_EQ(localRows().size(), 2);
	EXPECT_EQ(hero->manaRegain(), recovery);
	hero->removeBonus(localRows().front());
	ASSERT_EQ(localRows().size(), 1);
	hero->initHero(*handler.randomizer);
	EXPECT_EQ(localRows().size(), 2);
	EXPECT_EQ(hero->manaRegain(), recovery);
	EXPECT_EQ(hero->manaRegain(), expected(*hero));
	for(size_t index = 0; index < primary.size(); ++index)
		EXPECT_EQ(hero->getPrimSkillLevel(PrimarySkill(index)), primary[index]);
}

TEST_F(NewHorizonsManaRegenerationArtifactTest, OriginalModeKeepsLegacyRecovery)
{
	original = true;
	auto * hero = prepare();
	ASSERT_NE(hero, nullptr);
	EXPECT_FALSE(newHorizonsHeroes::capabilityArtifactManaRegeneration(hero->getCapabilityRules(), item(0), hero->manaLimit()));
	EXPECT_EQ(hero->manaRegain(), expected(*hero));
}

TEST_F(NewHorizonsManaRegenerationArtifactTest, CurrentCaptureAndBinaryHeroRoundTripPreserveTiers)
{
	auto * hero = prepare();
	ASSERT_NE(hero, nullptr);
	const auto captured = hero->getCapabilityRules()["artifactManaRegeneration"];
	EXPECT_EQ(captured, tierRules()["artifactManaRegeneration"]);
	CMemorySerializer wire;
	wire.oser & *hero;
	wire.iser.cb = gameState().get();
	CGHeroInstance decoded(gameState().get());
	wire.iser & decoded;
	EXPECT_EQ(decoded.getCapabilityRules()["artifactManaRegeneration"], captured);
	EXPECT_EQ(newHorizonsHeroes::capabilityArtifactManaRegeneration(decoded.getCapabilityRules(), item(0), hero->manaLimit()),
		newHorizonsHeroes::capabilityArtifactManaRegeneration(hero->getCapabilityRules(), item(0), hero->manaLimit()));
}

TEST_F(NewHorizonsManaRegenerationArtifactTest, PresenceGuardsRejectAllActualOuterPrefixes)
{
	auto * hero = prepare();
	ASSERT_NE(hero, nullptr);
	PrefixProbe heroWriter, mapWriter, worldWriter, lobbyWriter;
	EXPECT_THROW(hero->serialize(heroWriter), std::runtime_error);
	EXPECT_EQ(heroWriter.fields, 0u);
	EXPECT_THROW(map()->serialize(mapWriter), std::runtime_error);
	EXPECT_EQ(mapWriter.fields, 0u);
	EXPECT_THROW(gameState()->serialize(worldWriter), std::runtime_error);
	EXPECT_EQ(worldWriter.fields, 0u);
	LobbyStartGame lobby;
	lobby.initializedGameState = gameState();
	EXPECT_THROW(lobby.serialize(lobbyWriter), std::runtime_error);
	EXPECT_EQ(lobbyWriter.fields, 0u);
	GameSettings settings;
	settings.loadOverrides(JsonNode(JsonMap{}));
	PrefixProbe plain;
	EXPECT_NO_THROW(settings.serialize(plain));
	EXPECT_GT(plain.fields, 0u);
}

TEST_F(NewHorizonsManaRegenerationArtifactTest, ConfigurableTiersAndMalformedPresenceAreValidated)
{
	auto rules = tierRules();
	rules["artifactManaRegeneration"][ITEMS[0]]["minimum"].Integer() = 9;
	rules["artifactManaRegeneration"][ITEMS[0]]["percent"].Integer() = 7;
	EXPECT_EQ(newHorizonsHeroes::capabilityArtifactManaRegeneration(rules, item(0), 200), 14);
	for(int corruption = 0; corruption < 5; ++corruption)
	{
		auto invalid = rules;
		if(corruption == 0) invalid["artifactManaRegeneration"] = JsonNode();
		if(corruption == 1) invalid["artifactManaRegeneration"][ITEMS[0]]["minimum"].Integer() = -1;
		if(corruption == 2) invalid["artifactManaRegeneration"][ITEMS[0]]["percent"].Float() = 1.5;
		if(corruption == 3) invalid["artifactManaRegeneration"]["core:spellbook"] = invalid["artifactManaRegeneration"][ITEMS[0]];
		if(corruption == 4) invalid["artifactManaRegeneration"].Struct().erase(ITEMS[2]);
		EXPECT_THROW(newHorizonsHeroes::validateCapabilityRules(invalid, false), std::runtime_error);
		EXPECT_THROW(newHorizonsHeroes::validateArtifactManaRegenerationSerialization(invalid, false), std::runtime_error);
	}
	auto absent = rules;
	absent.Struct().erase("artifactManaRegeneration");
	EXPECT_NO_THROW(newHorizonsHeroes::validateArtifactManaRegenerationSerialization(absent, false));
	EXPECT_FALSE(newHorizonsHeroes::capabilityArtifactManaRegeneration(absent, item(0), 200));
}

TEST_F(NewHorizonsManaRegenerationArtifactTest, RawSettingsRejectUnsupportedPresenceBeforeInstallAndLegacyReadReplaces)
{
	const auto previous = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_ARTIFACT_MANA_REGENERATION) - 1);
	JsonNode current;
	current["heroes"]["newHorizonsCapabilities"] = tierRules();
	GameSettings settings;
	settings.loadOverrides(current);
	PrefixProbe writer;
	EXPECT_THROW(settings.serialize(writer), std::runtime_error);
	EXPECT_EQ(writer.fields, 0u);
	CMemorySerializer forged;
	forged.oser.version = previous;
	forged.iser.version = previous;
	forged.oser & current;
	GameSettings untouched;
	EXPECT_THROW(untouched.serialize(forged.iser), std::runtime_error);
	CMemorySerializer rejectedSnapshot;
	ASSERT_NO_THROW(untouched.serialize(rejectedSnapshot.oser));
	JsonNode rejectedOverrides;
	ASSERT_NO_THROW(rejectedSnapshot.iser & rejectedOverrides);
	EXPECT_TRUE(rejectedOverrides["heroes"]["newHorizonsCapabilities"].isNull());
	auto historical = current;
	historical["heroes"]["newHorizonsCapabilities"].Struct().erase("artifactManaRegeneration");
	CMemorySerializer old;
	old.oser.version = previous;
	old.iser.version = previous;
	old.oser & historical;
	ASSERT_NO_THROW(settings.serialize(old.iser));
	EXPECT_FALSE(settings.getValue(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES)
		.Struct().contains("artifactManaRegeneration"));
}

TEST_F(NewHorizonsManaRegenerationArtifactTest, WizardsWellKeepsItsUnrelatedFullPercentageRecovery)
{
	const auto well = ArtifactID(ArtifactID::decode("core:wizardsWell"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("ManaRecoveryWell").playerActive(PlayerColor(0))
		.playerActive(PlayerColor(1)).hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
		.heroPrimary(0, 0, 0, 100).heroEquipped({{ArtifactPosition::MISC1, well}});
	startWithMap(std::move(builder));
	const auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->valOfBonuses(BonusType::MANA_PERCENTAGE_REGENERATION), 100);
	EXPECT_EQ(hero->manaRegain(), hero->manaLimit());
}

#ifdef ENABLE_NULLKILLER2_AI
TEST_F(NewHorizonsManaRegenerationArtifactTest, AdventureAIValuesTheSameCapturedItemRecovery)
{
	auto * hero = prepare(200, false, false, true);
	ASSERT_NE(hero, nullptr);
	ASSERT_TRUE(hero->hasSpellbook());
	const auto * artifact = hero->getArt(ArtifactPosition::MISC1);
	ASSERT_NE(artifact, nullptr);
	EXPECT_EQ(NK2AI::getArtifactScoreForHero(hero, artifact),
		static_cast<int64_t>(*newHorizonsHeroes::capabilityArtifactManaRegeneration(
			hero->getCapabilityRules(), item(0), hero->manaLimit())) * 500);
}
#endif
