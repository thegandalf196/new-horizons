/*
 * RandomArtifactPoolConsumersTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include <gtest/gtest.h>
#include <tbb/global_control.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

#include "../../lib/GameSettings.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/entities/artifact/CArtHandler.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/mapObjects/CGMarket.h"
#include "../../lib/mapObjectConstructors/AObjectTypeHandler.h"
#include "../../lib/mapObjectConstructors/CObjectClassesHandler.h"
#include "../../lib/rmg/CMapGenOptions.h"
#include "../../lib/rmg/CMapGenerator.h"
#include "../../lib/rmg/CRmgTemplate.h"
#include "../../lib/serializer/JsonDeserializer.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../mock/TinyH3MBuilder.h"
#include "../mock/TinyMapGameTest.h"
#include "../mock/mock_IGameEventCallback.h"

namespace
{
using ArtifactSet = std::set<ArtifactID>;

ArtifactSet artifactSetForClass(EArtifactClass artifactClass)
{
	ArtifactSet result;
	for(const auto & id : LIBRARY->arth->getDefaultAllowed())
	{
		const auto * artifact = id.toArtifact();
		if(LIBRARY->arth->legalArtifact(id) && artifact->aClass == artifactClass)
			result.insert(id);
	}
	return result;
}

ArtifactID firstNonComboArtifact(EArtifactClass artifactClass)
{
	for(const auto & id : LIBRARY->arth->getDefaultAllowed())
	{
		const auto * artifact = id.toArtifact();
		if(LIBRARY->arth->legalArtifact(id) && artifact->aClass == artifactClass && artifact->getPartOf().empty())
			return id;
	}
	return ArtifactID::NONE;
}

JsonNode artifactSetting(const ArtifactSet & artifacts)
{
	JsonNode result;
	result.Vector();
	for(const auto & id : artifacts)
	{
		JsonNode entry;
		entry.String() = id.toArtifact()->getJsonKey();
		result.Vector().push_back(std::move(entry));
	}
	return result;
}

JsonNode gameSettingsForArtifactPool(const ArtifactSet & artifacts)
{
	JsonNode settings;
	settings["artifacts"]["randomPoolExclusions"] = artifactSetting(artifacts);
	return settings;
}

class InstalledArtifactPoolSetting
{
	std::unique_ptr<GameSettings> previous;

public:
	explicit InstalledArtifactPoolSetting(const ArtifactSet & exclusions)
	{
		auto config = LIBRARY->settingsHandler->getFullConfig();
		config["artifacts"]["randomPoolExclusions"] = artifactSetting(exclusions);
		auto replacement = std::make_unique<GameSettings>();
		replacement->loadBase(config);
		previous = std::move(LIBRARY->settingsHandler);
		LIBRARY->settingsHandler = std::move(replacement);
	}

	~InstalledArtifactPoolSetting()
	{
		LIBRARY->settingsHandler = std::move(previous);
	}
};

std::unique_ptr<CRmgTemplate> minimalTemplateWithGameSettings(const JsonNode & settings)
{
	JsonNode serialized;
	serialized.Struct();
	serialized["name"].String() = "Artifact pool candidates";
	serialized["description"].String() = "Minimal synthetic template for artifact-pool integration tests";
	serialized["minSize"].String() = "s";
	serialized["maxSize"].String() = "s";
	serialized["players"].String() = "2";
	serialized["humans"].String() = "1";
	serialized["settings"] = settings;
	serialized["allowedWaterContent"].Vector().emplace_back("none");

	JsonNode connection;
	connection["a"].String() = "1";
	connection["b"].String() = "2";
	serialized["connections"].Vector().push_back(std::move(connection));

	auto & zones = serialized["zones"];
	zones.Struct();
	for(int owner : {1, 2})
	{
		auto & zone = zones[std::to_string(owner)];
		zone["type"].String() = "playerStart";
		zone["size"].Integer() = 11;
		zone["owner"].Integer() = owner;
		zone["monsters"].String() = "weak";
		zone["playerTowns"]["castles"].Integer() = 1;
	}

	auto result = std::make_unique<CRmgTemplate>();
	result->setId("test:artifactPoolCandidates");
	JsonDeserializer reader(nullptr, serialized);
	result->serializeJson(reader);
	result->afterLoad();
	return result;
}

ArtifactSet generateQuestArtifactCandidates(const CRmgTemplate & mapTemplate)
{
	CMapGenOptions options;
	options.setMapTemplate(&mapTemplate);
	options.setHumanOrCpuPlayerCount(2);
	options.setCompOnlyPlayerCount(0);
	options.setTeamCount(0);
	options.setCompOnlyTeamCount(0);
	options.setWaterContent(EWaterContent::NONE);
	options.setMonsterStrength(EMonsterStrength::GLOBAL_NORMAL);
	options.setPlayerTypeForStandardPlayer(PlayerColor(0), EPlayerType::HUMAN);
	options.setPlayerTypeForStandardPlayer(PlayerColor(1), EPlayerType::AI);

	CMapGenerator generator(options, nullptr, 1337);
	auto generated = generator.generate();
	if(!generated)
		throw std::runtime_error("Artifact-pool fixture failed to generate its map");
	const auto & candidates = generator.getAllPossibleQuestArtifacts();
	return ArtifactSet(candidates.begin(), candidates.end());
}

class RandomArtifactPoolMerchantTest : public TinyMapGameTest
{
protected:
	JsonNode mapOverride;

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::ARTIFACTS_RANDOM_POOL_EXCLUSIONS, mapOverride);
	}

	void startGame(const ArtifactSet & exclusions)
	{
		mapOverride = artifactSetting(exclusions);

		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0))
			.town({12, 12, 0}, FactionID(FactionID::decode("core:castle")), PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(0));
		startWithMap(std::move(builder));
	}

	CGBlackMarket * addBlackMarket()
	{
		const auto handler = LIBRARY->objtypeh->getHandlerFor(Obj::BLACK_MARKET, MapObjectSubID(0));
		const auto templates = handler->getTemplates();
		if(templates.empty())
			return nullptr;

		auto instance = handler->create(gameState().get(), templates.front());
		auto * market = dynamic_cast<CGBlackMarket *>(instance.get());
		if(!market)
			return nullptr;

		instance->setAnchorPos({20, 20, 0});
		map()->generateUniqueInstanceName(instance.get());
		map()->addNewObject(std::move(instance));
		return market;
	}
};
}

class RandomArtifactPoolConsumers : public ::testing::TestWithParam<size_t> {};

TEST_P(RandomArtifactPoolConsumers, RmgQuestCandidatesUseTemplatePolicyAndPreserveExplicitEmptyOverride)
{
	tbb::global_control boundedWorkers(tbb::global_control::max_allowed_parallelism, GetParam());

	const auto excludedTreasure = firstNonComboArtifact(EArtifactClass::ART_TREASURE);
	ASSERT_NE(excludedTreasure, ArtifactID::NONE);
	ASSERT_TRUE(artifactSetForClass(EArtifactClass::ART_TREASURE).contains(excludedTreasure));

	{
		InstalledArtifactPoolSetting installed({});
		auto mapTemplate = minimalTemplateWithGameSettings(gameSettingsForArtifactPool({excludedTreasure}));
		const auto candidates = generateQuestArtifactCandidates(*mapTemplate);
		EXPECT_FALSE(candidates.contains(excludedTreasure));
	}

	{
		InstalledArtifactPoolSetting installed({excludedTreasure});
		auto mapTemplate = minimalTemplateWithGameSettings(gameSettingsForArtifactPool({}));
		const auto candidates = generateQuestArtifactCandidates(*mapTemplate);
		EXPECT_TRUE(candidates.contains(excludedTreasure));
	}
}

INSTANTIATE_TEST_SUITE_P(ConcurrencyLimits, RandomArtifactPoolConsumers, ::testing::Values(size_t{1}, size_t{2}));

TEST_F(RandomArtifactPoolMerchantTest, BlackMarketRefreshAppliesStockFromFilteredArtifactPools)
{
	const auto excludedMinor = firstNonComboArtifact(EArtifactClass::ART_MINOR);
	ASSERT_NE(excludedMinor, ArtifactID::NONE);
	startGame({excludedMinor});
	ASSERT_TRUE(gameState()->isAllowed(excludedMinor));
	ASSERT_EQ(gameState()->getRandomArtifactPoolExclusions(), ArtifactSet{excludedMinor});

	auto * market = addBlackMarket();
	ASSERT_NE(market, nullptr);

	// Black markets refresh on day one. The game's freshly initialized map uses day zero.
	gameState()->day = 1;
	GameEventCallbackMock events(this);
	GameRandomizer randomizer(*gameState());
	ArtifactSet expectedMinors = artifactSetForClass(EArtifactClass::ART_MINOR);
	std::erase_if(expectedMinors, [&](ArtifactID id) { return !gameState()->isAllowed(id); });
	ASSERT_TRUE(expectedMinors.erase(excludedMinor));
	ASSERT_FALSE(expectedMinors.empty());
	ArtifactSet seenMinors;
	// Reuse one randomizer so its least-used selection visits the entire pool.
	// A single refresh could omit the excluded artifact purely by chance.
	for(size_t refresh = 0; refresh <= expectedMinors.size(); ++refresh)
	{
		market->newTurn(events, randomizer);
		ASSERT_EQ(market->artifacts.size(), 7u);
		std::map<EArtifactClass, size_t> classCounts;
		for(const auto & id : market->artifacts)
		{
			EXPECT_NE(id, excludedMinor);
			EXPECT_TRUE(gameState()->isAllowed(id));
			++classCounts[id.toArtifact()->aClass];
			if(id.toArtifact()->aClass == EArtifactClass::ART_MINOR)
				seenMinors.insert(id);
		}
		EXPECT_EQ(classCounts[EArtifactClass::ART_TREASURE], 3u);
		EXPECT_EQ(classCounts[EArtifactClass::ART_MINOR], 3u);
		EXPECT_EQ(classCounts[EArtifactClass::ART_MAJOR], 1u);
	}
	EXPECT_EQ(seenMinors, expectedMinors);
}
