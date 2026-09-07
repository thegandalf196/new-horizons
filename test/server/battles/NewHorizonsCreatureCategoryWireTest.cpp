/*
 * NewHorizonsCreatureCategoryWireTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/campaign/CampaignState.h"
#include "../../../lib/gameState/TavernHeroesPool.h"
#include "../../../lib/gameState/CGameStateCampaign.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/entities/building/TownFortifications.h"
#include "../../../lib/rmg/CMapGenOptions.h"
#include "../../../lib/mapObjects/MiscObjects.h"
#include "../../../lib/mapObjects/ObjectTemplate.h"
#include "../../../lib/mapObjects/TownBuildingInstance.h"
#include "../../../lib/mapping/CCastleEvent.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Updaters.h"
#include <stdexcept>

using namespace newHorizonsCreatures;

namespace
{
constexpr auto missingEntity = "core:missingCategoryCreature";

/// Write every real field through the real binary serializer, substituting only
/// the category's single-JsonNode payload. This is adversarial native wire input,
/// not a GUI export, a save-fixture injection or a replacement loader.
class CategoryWireWriter
{
	BinarySerializer & writer;
	JsonNode replacement;
public:
	using Version = ESerializationVersion;
	static constexpr bool saving = true;
	static constexpr bool loadingGamestate = false;
	int replacements = 0;

	CategoryWireWriter(BinarySerializer & writer, JsonNode replacement)
		: writer(writer), replacement(std::move(replacement)) {}
	bool hasFeature(Version version) const { return writer.hasFeature(version); }

	template<typename T> CategoryWireWriter & operator&(T & value)
	{
		writer & value;
		return *this;
	}
	CategoryWireWriter & operator&(CreatureCategoryRules &)
	{
		++replacements;
		writer & replacement;
		return *this;
	}
};

JsonNode malformedCurrentRules(const CreatureCategoryRules & rules)
{
	auto result = rules.getRules();
	result["creatures"]["core:pixie"].String() = "champion";
	result["creatures"][missingEntity].String() = "elite";
	// Parsing must succeed: only canonical entity validation can reject this.
	const CreatureCategoryRules structurallyValid(result);
	return result;
}

template<typename Load> void expectEntityRejection(Load load)
{
	bool rejected = false;
	try
	{
		load();
	}
	catch(const std::runtime_error & error)
	{
		rejected = true;
		EXPECT_EQ(std::string(error.what()),
			std::string("Unknown or noncanonical creature category identifier: ") + missingEntity);
	}
	EXPECT_TRUE(rejected) << "Current-wire entity validation must reject before category replacement";
}
}

class NewHorizonsCreatureCategoryWireTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		useCommands = false;
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCreatureCategories")));
	}
};

TEST_F(NewHorizonsCreatureCategoryWireTest, WorldRejectsCurrentUnknownEntityAndPreservesPriorCategory)
{
	startGame();
	const CreatureID pixie(CreatureID::decode("core:pixie"));
	const auto prior = gameState()->getCreatureCategory(pixie);
	ASSERT_TRUE(prior);
	ASSERT_EQ(prior->category, CreatureCategory::CORE);
	CMemorySerializer wire;
	CategoryWireWriter writer(wire.oser, malformedCurrentRules(gameState()->getCreatureCategoryRules()));
	gameState()->serialize(writer);
	ASSERT_EQ(writer.replacements, 1);
	CGameState target;
	target.preInit(LIBRARY);
	target.loadFromMemory(gameState()->saveToMemory());
	ASSERT_EQ(target.getCreatureCategory(pixie), prior);
	wire.iser.cb = &target;
	wire.iser.loadingGamestate = true;
	expectEntityRejection([&] { wire.iser & target; });
	EXPECT_EQ(target.getCreatureCategory(pixie), prior);
	EXPECT_EQ(target.getCreatureCategoryRules().getRules(), gameState()->getCreatureCategoryRules().getRules());
	// Only the category publication guarantee is asserted, not transactional
	// rollback of all earlier world fields after a deliberately failed load.
}

TEST_F(NewHorizonsCreatureCategoryWireTest, BattleRejectsCurrentUnknownEntityAndPreservesPriorCategory)
{
	startGame();
	startBattle();
	const CreatureID pixie(CreatureID::decode("core:pixie"));
	const auto prior = battle()->battleGetCreatureCategory(pixie);
	ASSERT_TRUE(prior);
	ASSERT_EQ(prior->category, CreatureCategory::CORE);
	CMemorySerializer wire;
	CategoryWireWriter writer(wire.oser, malformedCurrentRules(battle()->getCreatureCategoryRules()));
	battle()->serialize(writer);
	ASSERT_EQ(writer.replacements, 1);
	BattleInfo target(gameState().get());
	ASSERT_EQ(target.battleGetCreatureCategory(pixie), prior);
	wire.iser.cb = gameState().get();
	expectEntityRejection([&] { wire.iser & target; });
	EXPECT_EQ(target.battleGetCreatureCategory(pixie), prior);
	EXPECT_EQ(target.getCreatureCategoryRules().getRules(), battle()->getCreatureCategoryRules().getRules());
}
