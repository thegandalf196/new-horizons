/*
 * RandomArtifactPoolExclusionsTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include <gtest/gtest.h>

#include <set>
#include <stdexcept>

#include "../../lib/GameSettings.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/campaign/CampaignState.h"
#include "../../lib/entities/artifact/CArtHandler.h"
#include "../../lib/entities/artifact/RandomArtifactPool.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/gameState/CGameStateCampaign.h"
#include "../../lib/gameState/QuestInfo.h"
#include "../../lib/gameState/TavernHeroesPool.h"
#include "../../lib/json/JsonRandom.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/MiscObjects.h"
#include "../../lib/mapObjects/ObjectTemplate.h"
#include "../../lib/mapObjects/Quest.h"
#include "../../lib/mapObjects/TownBuildingInstance.h"
#include "../../lib/mapping/CCastleEvent.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/rmg/CMapGenOptions.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/ESerializationVersion.h"
#include "../mock/TinyH3MBuilder.h"
#include "../mock/TinyMapGameTest.h"

namespace
{
using ArtifactSet = std::set<ArtifactID>;

ArtifactID artifact(const char * id)
{
	return ArtifactID(ArtifactID::decode(id));
}

ArtifactSet legacyTomes()
{
	return {
		artifact("core:tomeOfAirMagic"),
		artifact("core:tomeOfEarthMagic"),
		artifact("core:tomeOfFireMagic"),
		artifact("core:tomeOfWaterMagic")
	};
}

JsonNode settingFor(const ArtifactSet & ids)
{
	JsonNode setting;
	setting.Vector(); // Preserve an explicit [] override when ids is empty.
	for(const auto & id : ids)
	{
		JsonNode entry;
		entry.String() = id.toArtifact()->getJsonKey();
		setting.Vector().push_back(std::move(entry));
	}
	return setting;
}

ArtifactSet legalDefaultArtifacts()
{
	ArtifactSet result;
	for(const auto & id : LIBRARY->arth->getDefaultAllowed())
		if(LIBRARY->arth->legalArtifact(id))
			result.insert(id);
	return result;
}

ArtifactSet legalClassArtifacts(const CGameState & state, EArtifactClass artifactClass)
{
	ArtifactSet result;
	for(const auto & id : legalDefaultArtifacts())
		if(id.toArtifact()->aClass == artifactClass && state.isAllowed(id))
			result.insert(id);
	return result;
}

ArtifactID nonComboTreasure()
{
	for(const auto & id : legalDefaultArtifacts())
	{
		const auto * value = id.toArtifact();
		if(value->aClass == EArtifactClass::ART_TREASURE && value->getPartOf().empty())
			return id;
	}
	return ArtifactID::NONE;
}

std::string className(EArtifactClass value)
{
	switch(value)
	{
	case EArtifactClass::ART_SPECIAL: return "SPECIAL";
	case EArtifactClass::ART_TREASURE: return "TREASURE";
	case EArtifactClass::ART_MINOR: return "MINOR";
	case EArtifactClass::ART_MAJOR: return "MAJOR";
	case EArtifactClass::ART_RELIC: return "RELIC";
	}
	return {};
}

class InstalledArtifactPoolSetting
{
	std::unique_ptr<GameSettings> previous;
public:
	explicit InstalledArtifactPoolSetting(const JsonNode & exclusions)
	{
		auto config = LIBRARY->settingsHandler->getFullConfig();
		config["artifacts"]["randomPoolExclusions"] = exclusions;
		auto replacement = std::make_unique<GameSettings>();
		replacement->loadBase(config);
		previous = std::move(LIBRARY->settingsHandler);
		LIBRARY->settingsHandler = std::move(replacement);
	}
	~InstalledArtifactPoolSetting() { LIBRARY->settingsHandler = std::move(previous); }
};

class RandomArtifactPoolExclusionsTest : public TinyMapGameTest
{
protected:
	bool hasMapOverride = false;
	JsonNode mapOverride;

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		if(hasMapOverride)
			loaded->overrideGameSetting(EGameSettings::ARTIFACTS_RANDOM_POOL_EXCLUSIONS, mapOverride);
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0))
			.town({12, 12, 0}, FactionID(FactionID::decode("core:castle")), PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(0));
		startWithMap(std::move(builder));
	}

	void useMapPolicy(const ArtifactSet & exclusions)
	{
		hasMapOverride = true;
		mapOverride = settingFor(exclusions);
	}
};

void expectRandomizerCycle(const CGameState & state, const ArtifactSet & candidates, bool classPool)
{
	ASSERT_FALSE(candidates.empty());
	GameRandomizer randomizer(state);
	ArtifactSet seen;
	const ArtifactSet fullPool = classPool
		? legalClassArtifacts(state, candidates.begin()->toArtifact()->aClass)
		: legalDefaultArtifacts();
	for(size_t index = 0; index < fullPool.size() + 1; ++index)
	{
		const ArtifactID picked = classPool
			? randomizer.rollArtifact(candidates.begin()->toArtifact()->aClass)
			: randomizer.rollArtifact();
		EXPECT_TRUE(candidates.contains(picked)) << picked.toArtifact()->getJsonKey();
		seen.insert(picked);
	}
	EXPECT_EQ(seen, candidates);
}
}

TEST(RandomArtifactPoolExclusions, SettingParserAcceptsEmptyAndRejectsUnknownIds)
{
	EXPECT_TRUE(artifactRandomPool::exclusionsFromSetting(JsonNode()).empty());
	EXPECT_TRUE(artifactRandomPool::exclusionsFromSetting(settingFor(ArtifactSet{})).empty());

	JsonNode unknown;
	unknown.Vector().emplace_back("missing-mod:unknownArtifact");
	EXPECT_THROW(artifactRandomPool::exclusionsFromSetting(unknown), std::runtime_error);
}

TEST_F(RandomArtifactPoolExclusionsTest, ActivePolicyFiltersGenericAndClassRandomPoolsAndJsonRewards)
{
	const ArtifactSet tomes = legacyTomes();
	ArtifactSet exclusions = tomes;
	const auto treasure = nonComboTreasure();
	ASSERT_NE(treasure, ArtifactID::NONE);
	exclusions.insert(treasure);
	exclusions.insert(ArtifactID::GRAIL);
	useMapPolicy(exclusions);
	startGame();
	ASSERT_TRUE(gameState()->isAllowed(treasure));
	EXPECT_EQ(gameState()->getRandomArtifactPoolExclusions(), exclusions);

	const ArtifactSet allGeneric = legalDefaultArtifacts();
	for(const auto & tome : tomes)
		ASSERT_TRUE(allGeneric.contains(tome));
	ArtifactSet generic = allGeneric;
	artifactRandomPool::removeExclusions(generic, exclusions);
	expectRandomizerCycle(*gameState(), generic, false);

	const auto firstTome = *tomes.begin();
	const auto tomeClass = firstTome.toArtifact()->aClass;
	const ArtifactSet allRelics = legalClassArtifacts(*gameState(), tomeClass);
	for(const auto & tome : tomes)
	{
		if(tome.toArtifact()->aClass == tomeClass)
		{
			ASSERT_TRUE(allRelics.contains(tome));
		}
	}
	ArtifactSet relics = allRelics;
	artifactRandomPool::removeExclusions(relics, exclusions);
	expectRandomizerCycle(*gameState(), relics, true);

	ArtifactSet treasures = legalClassArtifacts(*gameState(), EArtifactClass::ART_TREASURE);
	ASSERT_TRUE(treasures.contains(treasure));
	GameRandomizer classRandomizer(*gameState());
	for(size_t index = 0; index < treasures.size(); ++index)
		EXPECT_NE(classRandomizer.rollArtifact(EArtifactClass::ART_TREASURE), treasure);

	GameRandomizer jsonRandomizer(*gameState());
	JsonRandom json(gameState().get(), jsonRandomizer);
	JsonNode classSelector;
	classSelector["class"].String() = "TREASURE";
	for(size_t index = 0; index < treasures.size() + 1; ++index)
		EXPECT_NE(json.loadArtifact(classSelector, {}), treasure);

	JsonNode explicitTome;
	explicitTome.String() = firstTome.toArtifact()->getJsonKey();
	EXPECT_EQ(json.loadArtifact(explicitTome, {}), firstTome);

	JsonNode nestedSelection;
	nestedSelection["type"]["class"].String() = className(tomeClass);
	nestedSelection["type"]["anyOf"].Vector().push_back(explicitTome);
	EXPECT_EQ(json.loadArtifact(nestedSelection, {}), firstTome);

	JsonNode impossibleExplicitBranch;
	JsonNode impossibleBranch;
	impossibleBranch["class"].String() = "TREASURE";
	impossibleBranch["anyOf"].Vector().push_back(explicitTome);
	impossibleExplicitBranch["anyOf"].Vector().push_back(impossibleBranch);
	JsonNode genericBranch;
	genericBranch["minValue"].Float() = 0;
	impossibleExplicitBranch["anyOf"].Vector().push_back(genericBranch);
	for(size_t index = 0; index < legalDefaultArtifacts().size() + 1; ++index)
		EXPECT_FALSE(tomes.contains(json.loadArtifact(impossibleExplicitBranch, {})));

	JsonNode noneOfTome;
	noneOfTome["class"].String() = className(tomeClass);
	noneOfTome["noneOf"].Vector().push_back(explicitTome);
	for(size_t index = 0; index < allRelics.size() + 1; ++index)
		EXPECT_NE(json.loadArtifact(noneOfTome, {}), firstTome);

	GameRandomizer emptyPoolRandomizer(*gameState());
	EXPECT_THROW(emptyPoolRandomizer.rollArtifact(ArtifactSet{}), std::runtime_error);
}

TEST_F(RandomArtifactPoolExclusionsTest, ExplicitEmptyMapPolicyClearsInheritedListAndPreservesLegacyRolls)
{
	const ArtifactSet inherited = legacyTomes();
	InstalledArtifactPoolSetting installed(settingFor(inherited));
	useMapPolicy({});
	startGame();
	EXPECT_TRUE(gameState()->getRandomArtifactPoolExclusions().empty());
	const auto & effectiveSetting = gameState()->getSettings().getValue(EGameSettings::ARTIFACTS_RANDOM_POOL_EXCLUSIONS);
	ASSERT_TRUE(effectiveSetting.isVector());
	EXPECT_TRUE(effectiveSetting.Vector().empty());

	const ArtifactSet generic = legalDefaultArtifacts();
	expectRandomizerCycle(*gameState(), generic, false);
	for(const auto & tome : legacyTomes())
		EXPECT_TRUE(generic.contains(tome));

	const auto firstTome = *legacyTomes().begin();
	const auto tomeClass = firstTome.toArtifact()->aClass;
	const ArtifactSet relics = legalClassArtifacts(*gameState(), tomeClass);
	expectRandomizerCycle(*gameState(), relics, true);
	for(const auto & tome : legacyTomes())
	{
		if(tome.toArtifact()->aClass == tomeClass)
		{
			EXPECT_TRUE(relics.contains(tome));
		}
	}

	GameRandomizer legacyFallback(*gameState());
	EXPECT_EQ(legacyFallback.rollArtifact(ArtifactSet{}), ArtifactID::GRAIL);
}

TEST_F(RandomArtifactPoolExclusionsTest, CapturedPolicySurvivesInstalledSettingsChangeOnCurrentSaveLoad)
{
	ArtifactSet exclusions = legacyTomes();
	const auto treasure = nonComboTreasure();
	ASSERT_NE(treasure, ArtifactID::NONE);
	exclusions.insert(treasure);
	InstalledArtifactPoolSetting installed(settingFor(exclusions));
	startGame();
	ASSERT_EQ(gameState()->getRandomArtifactPoolExclusions(), exclusions);
	auto saved = gameState()->saveToMemory();

	{
		InstalledArtifactPoolSetting changed(settingFor(ArtifactSet{}));
		CGameState restored;
		restored.preInit(LIBRARY);
		restored.loadFromMemory(std::move(saved));
		EXPECT_EQ(restored.getRandomArtifactPoolExclusions(), exclusions);
		const auto & changedSetting = restored.getSettings().getValue(EGameSettings::ARTIFACTS_RANDOM_POOL_EXCLUSIONS);
		ASSERT_TRUE(changedSetting.isVector());
		EXPECT_TRUE(changedSetting.Vector().empty());
	}
}

TEST_F(RandomArtifactPoolExclusionsTest, NonemptyPolicyCannotBeDowngraded)
{
	const auto oldVersion = static_cast<ESerializationVersion>(
		static_cast<int32_t>(ESerializationVersion::NEW_HORIZONS_RANDOM_ARTIFACT_POOL) - 1);

	useMapPolicy(legacyTomes());
	startGame();
	CMemorySerializer rejected;
	rejected.oser.version = oldVersion;
	EXPECT_THROW(rejected.oser & *gameState(), std::runtime_error);
}

TEST_F(RandomArtifactPoolExclusionsTest, OldSaveDefaultsCapturedPolicyToEmpty)
{
	const auto oldVersion = static_cast<ESerializationVersion>(
		static_cast<int32_t>(ESerializationVersion::NEW_HORIZONS_RANDOM_ARTIFACT_POOL) - 1);
	InstalledArtifactPoolSetting emptyBase(settingFor(ArtifactSet{}));
	startGame();
	CMemorySerializer writer;
	writer.oser.version = oldVersion;
	writer.oser & *gameState();
	auto bytes = writer.extractBuffer();

	InstalledArtifactPoolSetting newBase(settingFor(legacyTomes()));
	CGameState restored;
	restored.preInit(LIBRARY);
	CMemorySerializer reader(std::move(bytes));
	reader.iser.version = oldVersion;
	reader.iser.loadingGamestate = true;
	reader.iser.cb = &restored;
	reader.iser & restored;
	EXPECT_TRUE(restored.getRandomArtifactPoolExclusions().empty());
}
